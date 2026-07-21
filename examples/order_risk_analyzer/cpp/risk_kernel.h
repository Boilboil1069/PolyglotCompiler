#ifndef ORDER_RISK_ANALYZER_RISK_KERNEL_H
#define ORDER_RISK_ANALYZER_RISK_KERNEL_H

#include <stddef.h>

#if defined(_WIN32)
#define RISK_API __declspec(dllexport)
#else
#define RISK_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RiskOrder {
  double amount;
  int items;
  double distance_km;
  int customer_days;
  int chargebacks;
} RiskOrder;

typedef struct RiskDecision {
  int score;
  double shipping_cost;
} RiskDecision;

// Returns 0 on success and a negative value for an invalid pointer or field.
RISK_API int risk_evaluate(const RiskOrder *order, RiskDecision *decision);
RISK_API const char *risk_kernel_version(void);

#ifdef __cplusplus
}
#endif

#endif
