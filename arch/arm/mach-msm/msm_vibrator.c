/* include/asm/mach-msm/htc_pwrsink.h
 *
 * Copyright (C) 2008 HTC Corporation.
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2011 The Linux Foundation. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/hrtimer.h>
#include <linux/sched.h>
#include <linux/module.h>
#include "pmic.h"
#include "timed_output.h"

#include <mach/msm_rpcrouter.h>

#define PM_LIBPROG      0x30000061
#if (CONFIG_ARCH_MSM8610)
#define PM_LIBVERS      0xfb837d0b
#else
#define PM_LIBVERS      0x10001
#endif

#define HTC_PROCEDURE_SET_VIB_ON_OFF	21
#define PMIC_VIBRATOR_LEVEL_MAX 3100
#define PMIC_VIBRATOR_LEVEL_MIN 1100

static struct work_struct work_vibrator_on;
static struct work_struct work_vibrator_off;
static struct hrtimer vibe_timer;

/* default value for vibration intensity, 95% = 3000 in PMIC_VIBRATOR_LEVEL */
static unsigned long pwmval = 95;


#ifdef CONFIG_PM8XXX_RPC_VIBRATOR
static void set_pmic_vibrator(int on)
{
	int rc;

	rc = pmic_vib_mot_set_mode(PM_VIB_MOT_MODE__MANUAL);
	if (rc) {
		pr_err("%s: Vibrator set mode failed", __func__);
		return;
	}

	if (on)
		rc = pmic_vib_mot_set_volt(PMIC_VIBRATOR_LEVEL);
	else
		rc = pmic_vib_mot_set_volt(0);

	if (rc)
		pr_err("%s: Vibrator set voltage level failed", __func__);
}
#else
static void set_pmic_vibrator(int on)
{
	int pwm_duty;
	static struct msm_rpc_endpoint *vib_endpoint;
	struct set_vib_on_off_req {
		struct rpc_request_hdr hdr;
		uint32_t data;
	} req;

	if (!vib_endpoint) {
		vib_endpoint = msm_rpc_connect(PM_LIBPROG, PM_LIBVERS, 0);
		if (IS_ERR(vib_endpoint)) {
			printk(KERN_ERR "init vib rpc failed!\n");
			vib_endpoint = 0;
			return;
		}
	}

	/* make sure pwmval is between 0 and 100 */
	 if (pwmval > 100) {
	 pwmval = 100;
	 } else if (pwmval < 0) {
	 pwmval = 0;
	 }

	/* calculate vibration level */
	pwm_duty = (PMIC_VIBRATOR_LEVEL_MIN +
	(pwmval * (PMIC_VIBRATOR_LEVEL_MAX - PMIC_VIBRATOR_LEVEL_MIN) / 100));

	if (on)
		req.data = cpu_to_be32(pwm_duty);
	else
		req.data = cpu_to_be32(0);

	msm_rpc_call(vib_endpoint, HTC_PROCEDURE_SET_VIB_ON_OFF, &req,
		sizeof(req), 5 * HZ);
}
#endif

static void pmic_vibrator_on(struct work_struct *work)
{
	set_pmic_vibrator(1);
}

static void pmic_vibrator_off(struct work_struct *work)
{
	set_pmic_vibrator(0);
}

static void timed_vibrator_on(struct timed_output_dev *sdev)
{
	schedule_work(&work_vibrator_on);
}

static void timed_vibrator_off(struct timed_output_dev *sdev)
{
	schedule_work(&work_vibrator_off);
}

static void vibrator_enable(struct timed_output_dev *dev, int value)
{
	hrtimer_cancel(&vibe_timer);

	if (value == 0)
		timed_vibrator_off(dev);
	else {
		value = (value > 15000 ? 15000 : value);

		timed_vibrator_on(dev);

		hrtimer_start(&vibe_timer,
			      ktime_set(value / 1000, (value % 1000) * 1000000),
			      HRTIMER_MODE_REL);
	}
}

static int vibrator_get_time(struct timed_output_dev *dev)
{
	if (hrtimer_active(&vibe_timer)) {
		ktime_t r = hrtimer_get_remaining(&vibe_timer);
		struct timeval t = ktime_to_timeval(r);
		return t.tv_sec * 1000 + t.tv_usec / 1000;
	}
	return 0;
}

static enum hrtimer_restart vibrator_timer_func(struct hrtimer *timer)
{
	timed_vibrator_off(NULL);
	return HRTIMER_NORESTART;
}

static ssize_t pwmvalue_show(struct device *dev,
struct device_attribute *attr, char *buf)
{
int count;
count = sprintf(buf, "%lu\n", pwmval);
pr_info("vibrator: pwmval: %lu\n", pwmval);
return count;
}
ssize_t pwmvalue_store(struct device *dev,
struct device_attribute *attr,
const char *buf, size_t size)
{
if (kstrtoul(buf, 0, &pwmval))
pr_err("vibrator: error in storing pwm value\n");
pr_info("vibrator: pwmval: %lu\n", pwmval);
return size;
}
static DEVICE_ATTR(pwmvalue, S_IRUGO | S_IWUGO,
pwmvalue_show, pwmvalue_store);
static int atlas40_create_vibrator_sysfs(void)
{
int ret;
struct kobject *vibrator_kobj;
vibrator_kobj = kobject_create_and_add("vibrator", NULL);
if (unlikely(!vibrator_kobj))
return -ENOMEM;
ret = sysfs_create_file(vibrator_kobj,
&dev_attr_pwmvalue.attr);
if (unlikely(ret < 0)) {
pr_err("vibrator: sysfs_create_file failed: %d\n", ret);
return ret;
}
return 0;
}


static struct timed_output_dev pmic_vibrator = {
	.name = "vibrator",
	.get_time = vibrator_get_time,
	.enable = vibrator_enable,
};

void __init msm_init_pmic_vibrator(void)
{
	INIT_WORK(&work_vibrator_on, pmic_vibrator_on);
	INIT_WORK(&work_vibrator_off, pmic_vibrator_off);

	hrtimer_init(&vibe_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	vibe_timer.function = vibrator_timer_func;

	atlas40_create_vibrator_sysfs();

	timed_output_dev_register(&pmic_vibrator);
}

MODULE_DESCRIPTION("timed output pmic vibrator device");
MODULE_LICENSE("GPL");

