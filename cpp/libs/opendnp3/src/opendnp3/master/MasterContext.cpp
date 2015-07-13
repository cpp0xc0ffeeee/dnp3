/**
 * Licensed to Green Energy Corp (www.greenenergycorp.com) under one or
 * more contributor license agreements. See the NOTICE file distributed
 * with this work for additional information regarding copyright ownership.
 * Green Energy Corp licenses this file to you under the Apache License,
 * Version 2.0 (the "License"); you may not use this file except in
 * compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This project was forked on 01/01/2013 by Automatak, LLC and modifications
 * may have been made to this file. Automatak, LLC licenses these modifications
 * to you under the terms of the License.
 */

#include "MasterContext.h"

#include "opendnp3/app/APDULogging.h"
#include "opendnp3/LogLevels.h"
#include "opendnp3/master/MeasurementHandler.h"

#include <openpal/logging/LogMacros.h>


#include <assert.h>

using namespace openpal;

namespace opendnp3
{
	MContext::MContext(
		IExecutor& executor,
		LogRoot& root,
		ILowerLayer& lower,
		ISOEHandler& SOEHandler,
		opendnp3::IMasterApplication& application,		
		const MasterParams& params_,
		ITaskLock& taskLock
		) :

		logger(root.GetLogger()),
		pExecutor(&executor),
		pLower(&lower),
		params(params_),
		pSOEHandler(&SOEHandler),
		pTaskLock(&taskLock),
		pApplication(&application),		
		isOnline(false),
		isSending(false),		
		responseTimer(executor),
		scheduleTimer(executor),
		tasks(params, logger, application, SOEHandler, application),		
		txBuffer(params.maxTxFragSize),
		tstate(TaskState::IDLE)
	{}

	void MContext::CheckForTask()
	{
		if (isOnline)
		{
			this->tstate = this->OnStartEvent();
		}
	}

	void MContext::OnResponseTimeout()
	{
		if (isOnline)
		{
			this->tstate = this->OnResponseTimeoutEvent();
		}
	}

	void MContext::CompleteActiveTask()
	{
		if (this->pActiveTask.IsDefined())
		{
			if (this->pActiveTask->IsRecurring())
			{
				this->scheduler.Schedule(std::move(this->pActiveTask));
			}
			else
			{
				this->pActiveTask.Release();
			}

			pTaskLock->Release(*this);
			this->PostCheckForTask();
		}		
	}

	void MContext::OnReceive(const ReadBufferView& apdu, const APDUResponseHeader& header, const ReadBufferView& objects)
	{
		this->ProcessAPDU(header, objects);
	}

	void MContext::ProcessAPDU(const APDUResponseHeader& header, const ReadBufferView& objects)
	{
		switch (header.function)
		{
		case(FunctionCode::UNSOLICITED_RESPONSE) :
			this->ProcessUnsolicitedResponse(header, objects);
			break;
		case(FunctionCode::RESPONSE) :
			this->ProcessResponse(header, objects);
			break;
		default:
			FORMAT_LOG_BLOCK(this->logger, flags::WARN, "Ignoring unsupported function code: %s", FunctionCodeToString(header.function));
			break;
		}
	}

	void MContext::ProcessIIN(const IINField& iin)
	{
		if (iin.IsSet(IINBit::DEVICE_RESTART))
		{
			this->tasks.clearRestart.Demand();
			this->tasks.assignClass.Demand();
			this->tasks.startupIntegrity.Demand();
			this->tasks.enableUnsol.Demand();
		}

		if (iin.IsSet(IINBit::EVENT_BUFFER_OVERFLOW) && this->params.integrityOnEventOverflowIIN)
		{
			this->tasks.startupIntegrity.Demand();
		}

		if (iin.IsSet(IINBit::NEED_TIME))
		{
			this->tasks.timeSync.Demand();
		}

		if ((iin.IsSet(IINBit::CLASS1_EVENTS) && this->params.eventScanOnEventsAvailableClassMask.HasClass1()) ||
			(iin.IsSet(IINBit::CLASS2_EVENTS) && this->params.eventScanOnEventsAvailableClassMask.HasClass2()) ||
			(iin.IsSet(IINBit::CLASS3_EVENTS) && this->params.eventScanOnEventsAvailableClassMask.HasClass3()))
		{
			this->tasks.eventScan.Demand();
		}

		this->pApplication->OnReceiveIIN(iin);
	}

	void MContext::ProcessUnsolicitedResponse(const APDUResponseHeader& header, const ReadBufferView& objects)
	{
		if (!header.control.UNS)
		{
			SIMPLE_LOG_BLOCK(logger, flags::WARN, "Ignoring unsolicited response without UNS bit set");
			return;
		}

		auto result = MeasurementHandler::ProcessMeasurements(objects, logger, pSOEHandler);

		if ((result == ParseResult::OK) && header.control.CON)
		{
			this->QueueConfirm(APDUHeader::UnsolicitedConfirm(header.control.SEQ));
		}

		this->ProcessIIN(header.IIN);
	}

	void MContext::ProcessResponse(const APDUResponseHeader& header, const ReadBufferView& objects)
	{				
		this->tstate = this->OnResponseEvent(header, objects);		
		this->ProcessIIN(header.IIN); // TODO - should we process IIN bits for unexpected responses?
	}

	void MContext::QueueConfirm(const APDUHeader& header)
	{
		this->confirmQueue.push_back(header);
		this->CheckConfirmTransmit();
	}

	bool MContext::CheckConfirmTransmit()
	{
		if (this->isSending || this->confirmQueue.empty())
		{
			return false;
		}

		auto confirm = this->confirmQueue.front();
		APDUWrapper wrapper(this->txBuffer.GetWriteBufferView());
		wrapper.SetFunction(confirm.function);
		wrapper.SetControl(confirm.control);
		this->Transmit(wrapper.ToReadOnly());	
		this->confirmQueue.pop_front();
		return true;
	}

	void MContext::Transmit(const ReadBufferView& data)
	{
		logging::ParseAndLogRequestTx(this->logger, data);
		assert(!this->isSending);
		this->isSending = true;
		this->pLower->BeginTransmit(data);
	}

	void MContext::StartResponseTimer()
	{
		auto timeout = [this](){ this->OnResponseTimeout(); };
		this->responseTimer.Start(this->params.responseTimeout, timeout);
	}

	void MContext::PostCheckForTask()
	{
		auto callback = [this]() { this->CheckForTask(); };
		this->pExecutor->PostLambda(callback);
	}

	bool MContext::GoOffline()
	{
		if (isOnline)
		{
			auto now = pExecutor->GetTime();
			scheduler.Shutdown(now);

			if (pActiveTask.IsDefined())
			{
				pActiveTask->OnLowerLayerClose(now);
				pActiveTask.Release();
			}

			tstate = TaskState::IDLE;

			pTaskLock->OnLayerDown();			

			responseTimer.Cancel();

			solSeq = unsolSeq = 0;
			isOnline = isSending = false;

			return true;			
		}
		else
		{
			return false;
		}		
	}

	bool MContext::GoOnline()
	{
		if (isOnline)
		{
			return false;
		}
		else
		{
			isOnline = true;
			pTaskLock->OnLayerUp();			
			tasks.Initialize(scheduler);	
			this->PostCheckForTask();
			return true;
		}
	}

	bool MContext::BeginNewTask(ManagedPtr<IMasterTask>& task)
	{
		this->pActiveTask = std::move(task);				
		this->pActiveTask->OnStart();
		FORMAT_LOG_BLOCK(logger, flags::INFO, "Begining task: %s", this->pActiveTask->Name());
		return this->ResumeActiveTask();
	}

	bool MContext::ResumeActiveTask()
	{		
		if (this->pTaskLock->Acquire(*this))
		{
			APDURequest request(this->txBuffer.GetWriteBufferView());
			this->pActiveTask->BuildRequest(request, this->solSeq);
			this->StartResponseTimer();
			auto apdu = request.ToReadOnly();
			this->RecordLastRequest(apdu);			
			this->Transmit(apdu);
			return true;
		}
		else
		{
			return false;
		}
	}

	//// --- State tables ---

	MContext::TaskState MContext::OnResponseEvent(const APDUResponseHeader& header, const ReadBufferView& objects)
	{
		switch (tstate)
		{			
			case(TaskState::WAIT_FOR_RESPONSE) :
				return OnResponse_WaitForResponse(header, objects);
			default:
				FORMAT_LOG_BLOCK(logger, flags::WARN, "Not expecting a response, sequence: %u", header.control.SEQ);
				return tstate;
		}
	}
	
	MContext::TaskState MContext::OnStartEvent()
	{
		switch (tstate)
		{
			case(TaskState::IDLE) :
				return StartTask_Idle();
			case(TaskState::TASK_READY) :
				return StartTask_TaskReady();
			default:
				return tstate;
		}
	}
	
	MContext::TaskState MContext::OnResponseTimeoutEvent()
	{
		switch (tstate)
		{
			case(TaskState::WAIT_FOR_RESPONSE) :
				return OnResponseTimeout_WaitForResponse();
			default:
				SIMPLE_LOG_BLOCK(logger, flags::ERR, "Unexpected response timeout");
				return tstate;
		}
	}

	//// --- State actions ----

	MContext::TaskState MContext::StartTask_Idle()
	{
		if (this->isSending)
		{
			return TaskState::IDLE;
		}

		MonotonicTimestamp next;
		auto task = this->scheduler.GetNext(pExecutor->GetTime(), next);

		if (task.IsDefined())
		{
			return this->BeginNewTask(task) ? TaskState::WAIT_FOR_RESPONSE : TaskState::TASK_READY;
		}
		else
		{
			// restart the task timer			
			if (!next.IsMax())
			{
				scheduleTimer.Restart(next, [this](){this->CheckForTask(); });
			}
			return TaskState::IDLE;
		}		
	}

	MContext::TaskState MContext::StartTask_TaskReady()
	{
		if (this->isSending)
		{
			return TaskState::TASK_READY;
		}

		return this->ResumeActiveTask() ? TaskState::WAIT_FOR_RESPONSE : TaskState::TASK_READY;
	}

	MContext::TaskState MContext::OnResponse_WaitForResponse(const APDUResponseHeader& header, const ReadBufferView& objects)
	{
		if (header.control.SEQ != this->solSeq)
		{
			FORMAT_LOG_BLOCK(this->logger, flags::WARN, "Response with bad sequence: %u", header.control.SEQ);
			return TaskState::WAIT_FOR_RESPONSE;
		}
		
		this->responseTimer.Cancel();

		this->solSeq.Increment();

		auto now = this->pExecutor->GetTime();

		auto result = this->pActiveTask->OnResponse(header, objects, now);

		if (header.control.CON)
		{
			this->QueueConfirm(APDUHeader::SolicitedConfirm(header.control.SEQ));
		}

		switch (result)
		{
			case(IMasterTask::ResponseResult::OK_CONTINUE) :
				this->StartResponseTimer();
				return TaskState::WAIT_FOR_RESPONSE;
			case(IMasterTask::ResponseResult::OK_REPEAT) :
				return StartTask_TaskReady();
			default:
				// task completed or failed, either way go back to idle			
				this->CompleteActiveTask();
				return TaskState::IDLE;
		}
	}

	MContext::TaskState MContext::OnResponseTimeout_WaitForResponse()
	{
		auto now = this->pExecutor->GetTime();
		this->pActiveTask->OnResponseTimeout(now);
		this->solSeq.Increment();
		this->CompleteActiveTask();
		return TaskState::IDLE;
	}
}


