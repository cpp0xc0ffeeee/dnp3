#include <opendnp3/app/GroupVariationRecord.h>

#ifndef DNP3EX_RECORDHANDLER_HPP
#define DNP3EX_RECORDHANDLER_HPP

#include "dnp3ex/UpdateHandler.h"

namespace dnp3ex
{

class RecordHandler
{
public:
	
	virtual void startHandleRequest() =0;
	virtual void ProcessHeader(const opendnp3::AllObjectsHeader& header) =0;
	virtual void ProcessHeader(const opendnp3::RangeHeader& header) =0;
	virtual void ProcessHeader(const opendnp3::CountHeader& header) =0;
	virtual void beforeSendResponse(UpdateHandler& updateHandler) =0;
	virtual void onParseError() =0;

	virtual ~RecordHandler()= default;
};

}

#endif 
