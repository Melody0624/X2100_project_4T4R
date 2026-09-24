#ifndef SUPPLIER_PROFILE_OPTIONS_H
#define SUPPLIER_PROFILE_OPTIONS_H
/* Staging choices, not a claim about the manufacturer's build. */
#ifndef SUPPLIER_IS_MIRROR
#define SUPPLIER_IS_MIRROR 0
#endif
#ifndef SUPPLIER_USE_BPM
#define SUPPLIER_USE_BPM 1
#endif
#ifndef SUPPLIER_UART_ADC_SEND
#define SUPPLIER_UART_ADC_SEND 0
#endif
#define SUPPLIER_USE_USB_OUTPUT 1
#define SUPPLIER_CHEETAH_SAVE 0
#define SUPPLIER_SAVE_RAW_DATA 0
#define SUPPLIER_ADC_REPLAY 0
/* BPM table/start/reset are confirmed.  Per-TX DDMA mapping, remaining timing,
 * ADC layout semantics and array calibration still require validation. */
#define SUPPLIER_RF_ALGORITHM_CONFIRMED 0
#if (SUPPLIER_IS_MIRROR != 0 && SUPPLIER_IS_MIRROR != 1) || \
    (SUPPLIER_USE_BPM != 0 && SUPPLIER_USE_BPM != 1) || \
    (SUPPLIER_UART_ADC_SEND != 0 && SUPPLIER_UART_ADC_SEND != 1)
#error Supplier profile switches must be zero or one
#endif
#endif
