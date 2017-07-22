#include <opendnp3/app/GroupVariationRecord.h>

#ifndef DNP3EX_RECORDHANDLER_HPP
#define DNP3EX_RECORDHANDLER_HPP

namespace dnp3ex
{

class RecordHandler
{
public:
	
	virtual void ProcessHeader(const opendnp3::AllObjectsHeader& header) =0;
	virtual void ProcessHeader(const opendnp3::RangeHeader& header) =0;
	virtual void ProcessHeader(const opendnp3::CountHeader& header) =0;

	virtual ~RecordHandler()= default;
};

}

#endif 
