#ifndef _IT_TASK_PORT_H_
#define _IT_TASK_PORT_H_

#if defined(CONFIG_XBURST2)
#include "port_xburst2.h"
#elif defined(CONFIG_XBURST)
#include "port_xburst.h"
#else
#error "add your port here"
#endif

#endif /* _IT_TASK_PORT_H_ */
