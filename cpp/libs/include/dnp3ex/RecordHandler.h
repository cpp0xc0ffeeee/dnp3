
#ifndef DNP3EX_RECORDHANDLER_HPP
#define DNP3EX_RECORDHANDLER_HPP

#include "dnp3ex/UpdateHandler.h"
#include <opendnp3/app/GroupVariationRecord.h>
#include <opendnp3/outstation/DatabaseConfigView.h>

namespace dnp3ex
{

class RecordHandler
{
public:
	
	virtual void beforeSendResponse(opendnp3::DatabaseConfigView configView, UpdateHandler& updateHandler) =0;
	virtual void onParseError() =0;

	virtual ~RecordHandler()= default;
};

}

#endif 
