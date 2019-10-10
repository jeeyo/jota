#ifndef PROJECT_CONF_H_
#define PROJECT_CONF_H_

#undef RPL_CONF_MOP
#define RPL_CONF_MOP                RPL_MOP_STORING_NO_MULTICAST

#undef UIP_CONF_MAX_ROUTES
#define UIP_CONF_MAX_ROUTES         20

// #undef UIP_CONF_BUFFER_SIZE
// #define UIP_CONF_BUFFER_SIZE        256

// #undef ENERGEST_CONF_ON
// #define ENERGEST_CONF_ON            1

#undef UIP_CONF_STATISTICS
#define UIP_CONF_STATISTICS         1

#undef UIP_DS6_ROUTE_REMOVE_LEAST_RECENTLY_USED
#define UIP_DS6_ROUTE_REMOVE_LEAST_RECENTLY_USED  1

#define JOTA_BORDER_ROUTER

/* Set maximum debug level on all modules. See os/sys/log-conf.h for
 * a list of supported modules. The different log levels are defined in
 * os/sys/log.h:
 *     LOG_LEVEL_NONE         No log
 *     LOG_LEVEL_ERR          Errors
 *     LOG_LEVEL_WARN         Warnings
 *     LOG_LEVEL_INFO         Basic info
 *     LOG_LEVEL_DBG          Detailled debug
  */
#define LOG_CONF_LEVEL_IPV6                        LOG_LEVEL_ERR
// #define LOG_CONF_LEVEL_TCPIP                       LOG_LEVEL_ERR
// #define LOG_CONF_LEVEL_RPL                         LOG_LEVEL_ERR
// #define LOG_CONF_LEVEL_6LOWPAN                     LOG_LEVEL_ERR
// #define LOG_CONF_LEVEL_MAC                         LOG_LEVEL_ERR
// #define LOG_CONF_LEVEL_FRAMER                      LOG_LEVEL_ERR
// #define LOG_CONF_LEVEL_COAP                        LOG_LEVEL_ERR
// #define LOG_CONF_LEVEL_LWM2M                       LOG_LEVEL_ERR
// #define LOG_CONF_LEVEL_6TOP                        LOG_LEVEL_ERR

/* Enable cooja annotations */
// #define LOG_CONF_WITH_ANNOTATE 1

#endif /* PROJECT_CONF_H_ */
