#ifndef PROJECT_CONF_H_
#define PROJECT_CONF_H_

// #undef MAKE_ROUTING
// #define MAKE_ROUTING MAKE_ROUTING_RPL_LITE

#undef UIP_CONF_ROUTER
#define UIP_CONF_ROUTER              1

#undef UIP_CONF_UDP_CONNS
#define UIP_CONF_UDP_CONNS           20

/* Set maximum debug level on all modules. See os/sys/log-conf.h for
 * a list of supported modules. The different log levels are defined in
 * os/sys/log.h:
 *     LOG_LEVEL_NONE         No log
 *     LOG_LEVEL_ERR          Errors
 *     LOG_LEVEL_WARN         Warnings
 *     LOG_LEVEL_INFO         Basic info
 *     LOG_LEVEL_DBG          Detailled debug
 */
// #define LOG_CONF_LEVEL_TCPIP                       LOG_LEVEL_DBG
// #define LOG_CONF_LEVEL_RPL                         LOG_LEVEL_DBG
// #define LOG_CONF_LEVEL_6LOWPAN                     LOG_LEVEL_DBG
// #define LOG_CONF_LEVEL_MAC                         LOG_LEVEL_DBG
// #define LOG_CONF_LEVEL_FRAMER                      LOG_LEVEL_DBG
// #define LOG_CONF_LEVEL_COAP                        LOG_LEVEL_DBG
// #define LOG_CONF_LEVEL_LWM2M                       LOG_LEVEL_DBG
// #define LOG_CONF_LEVEL_6TOP                        LOG_LEVEL_DBG

/* Enable cooja annotations */
// #define LOG_CONF_WITH_ANNOTATE 1

#endif /* PROJECT_CONF_H_ */