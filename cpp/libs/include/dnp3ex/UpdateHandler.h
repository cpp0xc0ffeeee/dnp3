#ifndef DNP3EX_UPDATEHANDLER_H
#define DNP3EX_UPDATEHANDLER_H

#include <asiodnp3/Updates.h>

namespace dnp3ex
{

class UpdateHandler
{
public:
	
	virtual void applyUpdates(const asiodnp3::Updates& update) =0;
	virtual ~UpdateHandler()= default;
};

}

#endif 
