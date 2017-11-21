/*
 * jiq.c -- the just-in-queue module
 *
 * Copyright (C) 2001 Alessandro Rubini and Jonathan Corbet
 * Copyright (C) 2001 O'Reilly & Associates
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 *
 * $Id: jiq.c,v 1.7 2004/09/26 07:02:43 gregkh Exp $
 */
 
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/fs.h>     /* everything... */
#include <linux/proc_fs.h>
#include <linux/errno.h>  /* error codes */
#include <linux/workqueue.h>
#include <linux/preempt.h>
#include <linux/interrupt.h> /* tasklets */
#include <linux/seq_file.h>

MODULE_LICENSE("Dual BSD/GPL");

/*
 * The delay for the delayed workqueue timer file.
 */
static long delay = 1;
module_param(delay, long, 0);


/*
 * This module is a silly one: it only embeds short code fragments
 * that show how enqueued tasks `feel' the environment
 */

#define LIMIT	(PAGE_SIZE-128)	/* don't print any more after this size */

/*
 * Print information about the current environment. This is called from
 * within the task queues. If the limit is reched, awake the reading
 * process.
 */
static DECLARE_WAIT_QUEUE_HEAD (jiq_wait);


/*
 * Keep track of info we need between task queue runs.
 */
static struct clientdata {
	struct work_struct jiq_work;
	struct delayed_work jiq_delayed_work;	
	int cond;
	struct seq_file *seq_f;
	unsigned long jiffies;
	long delay;
} jiq_data;

#define SCHEDULER_QUEUE ((task_queue *) 1)



static void jiq_print_tasklet(unsigned long);
static DECLARE_TASKLET(jiq_tasklet, jiq_print_tasklet, (unsigned long)&jiq_data);


/*
 * Do the printing; return non-zero if the task should be rescheduled.
 */
static int jiq_print(struct clientdata* data)
{
	unsigned long j = jiffies;
	struct seq_file *filp = data->seq_f;

	if (filp->count > LIMIT) { 
		data->cond = 1;
		wake_up_interruptible(&jiq_wait);
		return 0;
	}

	if (filp->count == 0)
		seq_printf(data->seq_f,"    time  delta preempt   pid cpu command\n");

  	/* intr_count is only exported since 1.3.5, but 1.99.4 is needed anyways */
	seq_printf(data->seq_f, "%9li  %4li     %3i %5i %3i %s %d\n",
			j, j - data->jiffies,
			preempt_count(), current->pid, smp_processor_id(),
			current->comm, filp->count);

	data->jiffies = j;
	return 1;
}


/*
 * Call jiq_print from a work queue
 */
static void jiq_print_wq(struct work_struct *ptr)
{
	struct clientdata *data = container_of(ptr, struct clientdata, jiq_work);
	if (! jiq_print (data))
		return;
    
	if (data->delay)
		schedule_delayed_work(&(data->jiq_delayed_work), data->delay);
	else
		schedule_work(ptr);
}


static void jiq_print_wq_delay(struct work_struct *ptr)
{
	struct clientdata *data = container_of(ptr, struct clientdata, jiq_delayed_work.work);
	
	if (! jiq_print (data))
		return;

	if (data->delay)
		schedule_delayed_work(&(data->jiq_delayed_work), data->delay);
	else
		schedule_work(&(data->jiq_work));
}


static int jiq_read_wq(struct seq_file* seq, void *data)
{
	DEFINE_WAIT(wait);
	
	jiq_data.jiffies = jiffies;      /* initial time */
	jiq_data.delay = 0;
	jiq_data.seq_f = seq;
    
	prepare_to_wait(&jiq_wait, &wait, TASK_INTERRUPTIBLE);
	schedule_work(&(jiq_data.jiq_work));
	schedule();
	finish_wait(&jiq_wait, &wait);

	return 0;
}

static int jiq_read_wq_open(struct inode* inode, struct file *filp)
{
	return single_open(filp, jiq_read_wq, NULL);
}

static struct file_operations jiq_read_wq_fops = {
	.open = jiq_read_wq_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};



static int jiq_read_wq_delayed(struct seq_file* seq, void *data)
{
	DEFINE_WAIT(wait);
	
	jiq_data.jiffies = jiffies;      /* initial time */
	jiq_data.delay = delay;
	jiq_data.seq_f = seq;
	
	prepare_to_wait(&jiq_wait, &wait, TASK_INTERRUPTIBLE);
	schedule_delayed_work(&(jiq_data.jiq_delayed_work), delay);
	schedule();
	finish_wait(&jiq_wait, &wait);

	return 0;
}

static int jiq_read_wq_delay_open(struct inode* inode, struct file *filp)
{
	return single_open(filp, jiq_read_wq_delayed, NULL);
}

static struct file_operations jiq_read_wq_delay_fops = {
	.open = jiq_read_wq_delay_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};


/*
 * Call jiq_print from a tasklet
 */
static void jiq_print_tasklet(unsigned long ptr)
{
	if (jiq_print ((struct clientdata*) ptr))
		tasklet_schedule (&jiq_tasklet);
}



static int jiq_read_tasklet(struct seq_file* seq, void *data)
{
	jiq_data.jiffies = jiffies;      /* initial time */
	jiq_data.seq_f = seq;
	jiq_data.cond = 0;

	tasklet_schedule(&jiq_tasklet);
	wait_event_interruptible(jiq_wait, jiq_data.cond);

	return 0;
}

static int jiq_read_tasklet_open(struct inode* inode, struct file *filp)
{
	return single_open(filp, jiq_read_tasklet, NULL);
}

static struct file_operations jiq_read_tasklet_fops = {
	.open = jiq_read_tasklet_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};


/*
 * This one, instead, tests out the timers.
 */

static struct timer_list jiq_timer;

static void jiq_timedout(unsigned long ptr)
{
	struct clientdata *data = (struct clientdata*) ptr;
	jiq_print(data);            /* print a line */

	data->cond = 1;
	wake_up_interruptible(&jiq_wait);  /* awake the process */
}


static int jiq_read_run_timer(struct seq_file* seq, void *data)
{

	jiq_data.jiffies = jiffies;
	jiq_data.seq_f = seq;
	jiq_data.cond = 0;

	init_timer(&jiq_timer);              /* init the timer structure */
	jiq_timer.function = jiq_timedout;
	jiq_timer.data = (unsigned long)&jiq_data;
	jiq_timer.expires = jiffies + HZ; /* one second */

	jiq_print(&jiq_data);   /* print and go to sleep */
	add_timer(&jiq_timer);
	wait_event_interruptible(jiq_wait, jiq_data.cond);
	del_timer_sync(&jiq_timer);  /* in case a signal woke us up */
    
	return 0;
}

static int jiq_read_run_timer_open(struct inode* inode, struct file *filp)
{
	return single_open(filp, jiq_read_run_timer, NULL);
}

static struct file_operations jiq_read_run_timer_fops = {
	.open = jiq_read_run_timer_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};


/*
 * the init/clean material
 */

static int jiq_init(void)
{

	/* this line is in jiq_init() */
	INIT_WORK(&(jiq_data.jiq_work), jiq_print_wq);
	INIT_DELAYED_WORK(&(jiq_data.jiq_delayed_work), jiq_print_wq_delay);

	proc_create("jiqwq", 0, NULL, &jiq_read_wq_fops);
	proc_create("jiqwqdelay", 0, NULL, &jiq_read_wq_delay_fops);
	proc_create("jitimer", 0, NULL, &jiq_read_run_timer_fops);
	proc_create("jiqtasklet", 0, NULL, &jiq_read_tasklet_fops);

	return 0; /* succeed */
}

static void jiq_cleanup(void)
{
	remove_proc_entry("jiqwq", NULL);
	remove_proc_entry("jiqwqdelay", NULL);
	remove_proc_entry("jitimer", NULL);
	remove_proc_entry("jiqtasklet", NULL);
}


module_init(jiq_init);
module_exit(jiq_cleanup);
