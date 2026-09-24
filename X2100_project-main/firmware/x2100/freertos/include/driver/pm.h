#ifndef _PM_H_
#define _PM_H_

/**
 * @brief 进入睡眠模式(低功耗模式)
 */
void pm_enter_sleep(void);

void pm_power_off(void);

void pm_init(void);


/**
 * @brief 获取上一次唤醒的中断源
 * @return 成功返回中断号, 失败返回0。中断号范围为 enum soc_irq_type
 */
int pm_get_wakeup_irq(void);

#endif /* _PM_H_ */
