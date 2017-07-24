Overview
========

forked from [OpenDNP3](https://github.com/automatak/dnp3)

add read request handler
dnp3ex::RecordHandler
dnp3ex::UpdateHandler

```
class RecordHandler
{
public:
	
	//called before parsing object header
	virtual void startHandleRequest() =0;

	virtual void ProcessHeader(const opendnp3::AllObjectsHeader& header) =0;

	virtual void ProcessHeader(const opendnp3::RangeHeader& header) =0;

	virtual void ProcessHeader(const opendnp3::CountHeader& header) =0;

	//called before send read response, won't be called if got error from parsing object header
	//use updateHandler update database before send read response
	virtual void beforeSendResponse(UpdateHandler& updateHandler) =0;

	//called if got parse error
	virtual void onParseError() =0;

	virtual ~RecordHandler()= default;
};

```

Usage
=====
```
	#include <dnp3ex/RecordHandler.h>
	
	//...

	std::shared_ptr<dnp3ex::RecordHandler> recordHandler;


	auto outstation = channel->AddOutstation(
		"outstation",                             // alias for logging
	    SuccessCommandHandler::Create(),          // ICommandHandler (interface)
		DefaultOutstationApplication::Create(),   // IOutstationApplication (interface)
	    stackConfig,                              // static stack configuration
		recordHandler                             // add read handler here
	);
```

    

