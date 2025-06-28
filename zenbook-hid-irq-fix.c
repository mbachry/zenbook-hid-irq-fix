#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/device/bus.h>
#include <linux/irq.h>
#include <linux/kprobes.h>
#include <linux/cpumask.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>

#define BUS_NAME  "AMDI0010:01"
#define ADAPTER_NAME  "i2c-1"
#define DEVICE_NAME  "i2c-ASUE140D:00"

static const u32 THRESHOLD = 40000;
static const u32 TIMER_INTERVAL = 20 * HZ;

static struct irq_desc *(*my_irq_to_desc)(unsigned int irq);

static int hid_irq_num;
static int previous_irq_count;

static void irq_timer_callback(struct timer_list *tl);
static DEFINE_TIMER(irq_timer, irq_timer_callback);
static struct work_struct reset_work;


static struct kprobe irq_kprobe = {
	.symbol_name = "irq_to_desc",
	.flags = KPROBE_FLAG_DISABLED
};

static struct device *get_hid_device(void)
{
	struct device *adapter_dev = bus_find_device_by_name(&i2c_bus_type, NULL, ADAPTER_NAME);
	if (!adapter_dev) {
		pr_err("zenbook-hid-irq-fix: bus_find_device_by_name failed\n");
		return NULL;
	}

	struct device *hid_dev = device_find_child_by_name(adapter_dev, DEVICE_NAME);
	put_device(adapter_dev);
	if (!hid_dev) {
		pr_err("zenbook-hid-irq-fix: device_find_child_by_name failed\n");
		return NULL;
	}

	if (hid_dev->type != &i2c_client_type) {
		pr_err("zenbook-hid-irq-fix: not an i2c client\n");
		put_device(hid_dev);
		return NULL;
	}

	return hid_dev;
}

static void reset_hid_device(struct work_struct *work)
{
	struct device *hid_dev = get_hid_device();
	if (!hid_dev) {
		pr_err("zenbook-hid-irq-fix: failed to reset hid device\n");
		return;
	}

	/* runtime suspend of zenbook's touchpad powers it
           off. suspend and resume to power cycle the device */
	struct dev_pm_ops *ops = &hid_dev->pm_domain->ops;
	int res = ops->suspend(hid_dev);
	if (res < 0) {
		pr_err("zenbook-hid-irq-fix: failed to suspend: %d\n", res);
		goto out;
	}
	res = ops->resume(hid_dev);
	if (res < 0) {
		pr_err("zenbook-hid-irq-fix: failed to resume: %d\n", res);
		goto out;
	}

 out:
	put_device(hid_dev);
}

static int get_total_irq_count(int irq)
{
	rcu_read_lock();

	struct irq_desc *desc = my_irq_to_desc(irq);
	if (!desc) {
		rcu_read_unlock();
		return -ENODEV;
	}

	int total = 0;
	int cpu;
        for_each_online_cpu(cpu) {
		total += irq_desc_kstat_cpu(desc, cpu);
        }

	rcu_read_unlock();

	if (WARN_ON_ONCE(total < 0))
		return -EINVAL;

	return total;
}

static void irq_timer_callback(struct timer_list *tl)
{
	int total = get_total_irq_count(hid_irq_num);
	if (total < 0) {
		pr_err("zenbook-hid-irq-fix: bad interrupt: %d\n", total);
		goto out;
	}

	int diff = total - previous_irq_count;
	previous_irq_count = total;
	if (diff < 0)
		diff = 0;

	if (diff > THRESHOLD) {
		printk(KERN_INFO "zenbook-hid-irq-fix: %d > %d, resetting hid device\n", diff, THRESHOLD);
		cancel_work(&reset_work);
		schedule_work(&reset_work);
	}

 out:
        mod_timer(&irq_timer, jiffies + TIMER_INTERVAL);
}

static int __init zen_init_module(void)
{
	if (register_kprobe(&irq_kprobe) < 0) {
		pr_err("zenbook-hid-irq-fix: kallsyms lookup failed\n");
		return -ENODEV;
	}
	my_irq_to_desc = (struct irq_desc *(*)(unsigned int))irq_kprobe.addr;
	unregister_kprobe(&irq_kprobe);

	struct device *i2c_dev = bus_find_device_by_name(&platform_bus_type, NULL, BUS_NAME);
	if (!i2c_dev) {
		pr_err("zenbook-hid-irq-fix: bus_find_device_by_name failed\n");
		return -1;
	}

	hid_irq_num = platform_get_irq(to_platform_device(i2c_dev), 0);
	put_device(i2c_dev);

	previous_irq_count = get_total_irq_count(hid_irq_num);
	if (previous_irq_count < 0) {
		pr_err("zenbook-hid-irq-fix: bad interrupt: %d\n", previous_irq_count);
		return -ENODEV;
	}

	INIT_WORK(&reset_work, reset_hid_device);
	add_timer(&irq_timer);
        mod_timer(&irq_timer, jiffies + TIMER_INTERVAL);

	return 0;
}

static void __exit zen_cleanup_module(void)
{
	timer_delete_sync(&irq_timer);
	cancel_work_sync(&reset_work);
}

module_init(zen_init_module);
module_exit(zen_cleanup_module);
MODULE_DESCRIPTION("Fix for interrupts flood from zenbook touchpad");
MODULE_LICENSE("GPL");
