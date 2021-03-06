/* drivers/input/touchscreen/rohm_bu21018.c
 *
 * ROHM BU21018 touchscreen driver
 *
 * Copyright (C) 2010 ROHM SEMICONDUCTOR Co. Ltd.
 *
 * Author: CT Cheng <ct.cheng@rohm.com.tw>
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <asm/irq.h>
#include <linux/platform_device.h>
#include <mach/board.h>
//#include "BU21023_firmware.h"
#include "bu21023_FF_1022_chat.h"
#include "calibration_ts.h" 



#include <linux/input/mt.h>
#include <linux/async.h>
#include <linux/workqueue.h>

#include <linux/slab.h>  //for kzalloc


#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#define MAX_SUPPORT_POINT 2

//ROHM setting
/* Parameters for JP 4.3 INCH PANEL */
//#define JP_R4_3_20101028
#define SPREADWAY_8_INCH

#ifdef JP_R4_3_20101028
#define ROHM_TS_ABS_X_MIN 	78
#define ROHM_TS_ABS_X_MAX 	961
#define ROHM_TS_ABS_Y_MIN 	99
#define ROHM_TS_ABS_Y_MAX 	965
#endif

#ifdef SPREADWAY_8_INCH
#if 0
#define ROHM_TS_ABS_X_MIN 	78
#define ROHM_TS_ABS_X_MAX 	961
#define ROHM_TS_ABS_Y_MIN 	99
#define ROHM_TS_ABS_Y_MAX 	965
#else
#define ROHM_TS_ABS_X_MIN       172//162
#define ROHM_TS_ABS_X_MAX       904//931
#define ROHM_TS_ABS_Y_MIN       33//40
#define ROHM_TS_ABS_Y_MAX       968//934
#endif
#endif

#define ROHM_TS_I2C_SPEED (400 * 1000)

#define LCD_MAX_WIDTH 1024
#define LCD_MAX_HEIGH 600

extern int screen_x[5] ;
extern int screen_y[5] ;
extern int uncali_x_default[5] ;
extern int uncali_y_default[5];

/* Print the coordinate of touch point in the debug console */

//#define SHOW_TOUCH_POINT
#define TWO_COUNT 20 


/* Using software moving average */
#define SW_AVG

/********************************************/

#define HJC_DEBUG			0
#if HJC_DEBUG
	#define dbg(msg...)	printk(msg);
#else
	#define dbg(msg...)
#endif

#ifdef SW_AVG

/* Size of Moving Average buffer array */
#define BUFF_ARRAY  16

#define DISTANCE_X 32
#define DISTANCE_Y 32
#define AVG_BOUND_X 16
#define AVG_BOUND_Y 16
#define S_DISTANCE_X 6
#define S_DISTANCE_Y 6
#define S_AVG_BOUND_X 4
#define S_AVG_BOUND_Y 4

/* skip times of moving average when touch starting */
#define SKIP_AVG_1 5
#define SKIP_AVG_2 5
#endif


struct touch_point
{
    unsigned int x;
    unsigned int y;

#ifdef SW_AVG
    unsigned int buff_x[BUFF_ARRAY];
    unsigned int buff_y[BUFF_ARRAY];
    unsigned char buff_cnt;
    /* Previous coordinate of touch point after moving average calculating */
    unsigned int old_avg_x;
    unsigned int old_avg_y;
#endif


};

volatile struct adc_point gADPoint;

struct touch_point tp1, tp2;
static int	finger_count_1 =0;
static int	finger_count_2 =0;

/********************************************/

static struct workqueue_struct *rohm_wq;

struct rohm_ts_data 
{
	uint16_t addr;
	struct i2c_client *client;
	struct input_dev *input_dev;
	int irq;
	int use_irq;
	struct hrtimer timer;
	struct work_struct  work;
	uint32_t flags;

	void 	(*hw_init)(void);
	void 	(*hw_power)(int on);
	void 	(*hw_reset)(void);

#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
};

static struct rohm_ts_data *TS;
static int GPIO_IRQ;



#ifdef SW_AVG

//-----------------------------------------------------------------------------
//
//Coord_Avg
//
//-----------------------------------------------------------------------------
static void Coord_Avg (struct touch_point *tp)
{

	unsigned long temp_x = 0, temp_y = 0, temp_n = 0;
	unsigned int i;
	////////////////////////////////////////
	// X1,Y1 Moving Avg
	////////////////////////////////////////
	if((tp->x != 0) && (tp->y != 0))
	{			               			  			      				
		if(tp->buff_cnt >= SKIP_AVG_1)
		{  
		
		    if(((abs(tp->buff_x[0] - tp->x) > DISTANCE_X) && (abs(tp->buff_y[0] - tp->y) > DISTANCE_Y)) ||
		       ((abs(tp->buff_x[0] - tp->x) > S_DISTANCE_X) && (abs(tp->buff_y[0] - tp->y) < S_DISTANCE_Y)) ||
			   ((abs(tp->buff_x[0] - tp->x) < S_DISTANCE_X) && (abs(tp->buff_y[0] - tp->y) > S_DISTANCE_Y)) ||			   
			   (((tp->old_avg_x != 0) && (abs(tp->old_avg_x - tp->x) > AVG_BOUND_X)) && 
			   ( (tp->old_avg_y != 0) && (abs(tp->old_avg_y - tp->y) > AVG_BOUND_Y))) ||
			   (((tp->old_avg_x != 0) && (abs(tp->old_avg_x - tp->x) > S_AVG_BOUND_X)) && 			
			   ( (tp->old_avg_y != 0) && (abs(tp->old_avg_y - tp->y) < S_AVG_BOUND_Y)))||
			   (((tp->old_avg_x != 0) && (abs(tp->old_avg_x - tp->x) < S_AVG_BOUND_X)) && 			
			   ( (tp->old_avg_y != 0) && (abs(tp->old_avg_y - tp->y) > S_AVG_BOUND_Y))))
			{
				for (i = 0; i < tp->buff_cnt; i++)
				{
					tp->buff_x[tp->buff_cnt - i] = tp->buff_x[tp->buff_cnt - i - 1];
					tp->buff_y[tp->buff_cnt - i] = tp->buff_y[tp->buff_cnt - i - 1];
				}
				tp->buff_x[0] = tp->x;
				tp->buff_y[0] = tp->y;
 
				temp_x = 0; temp_y = 0; temp_n = 0;
        
				for (i = 0; i <= tp->buff_cnt; i++)
				{
					temp_x += ((unsigned long) (tp->buff_x[i] * (tp->buff_cnt - i + 1)));
					temp_y += ((unsigned long) (tp->buff_y[i] * (tp->buff_cnt - i + 1)));
					temp_n += (unsigned long) (tp->buff_cnt - i + 1);
				}            
				tp->x = temp_x / temp_n;
				tp->y = temp_y / temp_n;
		
				tp->old_avg_x = tp->x;
				tp->old_avg_y = tp->y;  	
				if(tp->buff_cnt < (BUFF_ARRAY-1))
					tp->buff_cnt++;
			}
			else 
			{	  
				tp->x = tp->old_avg_x;
				tp->y = tp->old_avg_y;	
			} 
		}
		else
		{
			for (i = 0; i < tp->buff_cnt; i++)
			{
				tp->buff_x[tp->buff_cnt - i] = tp->buff_x[tp->buff_cnt - i - 1];
				tp->buff_y[tp->buff_cnt - i] = tp->buff_y[tp->buff_cnt - i - 1];
			}	
			tp->buff_x[0] = tp->x;
			tp->buff_y[0] = tp->y;
			if(tp->buff_cnt < (BUFF_ARRAY-1))
				tp->buff_cnt++;
			tp->old_avg_x = tp->x;
			tp->old_avg_y = tp->y;
			tp->x = 0;
			tp->y = 0;			

		}
	}//End/ of "if((x1 != 0) && (y1 != 0))"
	else 
	{
		tp->buff_cnt = 0;
		if((tp->buff_x[0] != 0) && (tp->buff_y[0] != 0))
		{
			tp->x = tp->buff_x[0];
			tp->y = tp->buff_y[0];
		}
		else
		{
			tp->x = 0;
			tp->y = 0;
		}
		tp->buff_x[0] = 0;
		tp->buff_y[0] = 0;
		tp->old_avg_x = 0;
		tp->old_avg_y = 0;
	}


}
#endif
/*read */
static void read_from_ic()
{

	//define ouput data start
	unsigned int finger = 0 ;
	unsigned int touch = 0 ;
 	unsigned int gesture = 0 ;
	unsigned int X1,Y1 = 0;
        unsigned int X2,Y2 = 0;
 	//end of define data
	
	int ret,i;
	int bad_data = 0;
	struct i2c_msg msg[2];
	uint8_t buf[10];
	//struct rohm_ts_data *ts = container_of(work, struct rohm_ts_data, work);

	msg[0].addr = TS->client->addr;				//For BUMV21018MWV test only
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = buf;
	msg[0].scl_rate = ROHM_TS_I2C_SPEED;

    buf[0] = 0x20;  //I2C slave address

	msg[1].addr = TS->client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = sizeof(buf);
	msg[1].buf = buf;
	msg[1].scl_rate = ROHM_TS_I2C_SPEED;
#if 0
	ret = i2c_transfer(TS->client->adapter, msg, 2);

	if (ret < 0) 
	{
		bad_data = 1;
		goto done;
	} 
	else
	{
		bad_data = 0;

        /*XY coordinate */
        tp1.y = buf[1] | ((uint16_t)buf[0] << 2); 
		tp1.x = buf[3] | ((uint16_t)buf[2] << 2);
		tp2.y = buf[5] | ((uint16_t)buf[4] << 2); 
		tp2.x = buf[7] | ((uint16_t)buf[6] << 2);

		touch = buf[8];
		gesture = buf[9];

		}
#endif	
	printk("\n %s,tp1.x=%d,tp1.y=%d,tp2.x=%d,tp2.y=%d,touch=%d,gesture=%d\n",__FUNCTION__,tp1.x,tp1.y,tp2.x,tp2.y,touch,gesture);
	printk("===enable_irq===\n");
	i2c_smbus_write_byte_data(TS->client, 0x3E, 0xFF);//delete by hjc for test 2-10
	//enable_irq(TS->irq);

done:
	;	
}



static void rohm_ts_work_func(struct work_struct *work)
{
	//define ouput data start
	unsigned int finger = 0 ;
	unsigned int touch = 0 ;
 	unsigned int gesture = 0 ;
	unsigned int X1,Y1 = 0;
        unsigned int X2,Y2 = 0;
 	//end of define data
	
	int ret,i;
	int bad_data = 0;
	struct i2c_msg msg[2];
	uint8_t buf[10];
	struct rohm_ts_data *ts = container_of(work, struct rohm_ts_data, work);
	//1'nd data
	msg[0].addr = ts->client->addr;				//For BUMV21018MWV test only
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = buf;
	msg[0].scl_rate = ROHM_TS_I2C_SPEED;

    buf[0] = 0x20;  //I2C slave address

	msg[1].addr = ts->client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = sizeof(buf);
	msg[1].buf = buf;
	msg[1].scl_rate = ROHM_TS_I2C_SPEED;
	ret = i2c_transfer(ts->client->adapter, msg, 2);

//	printk(" msg[0].addr = %x  \n", msg[0].addr);
//	printk(" msg[0].buf = %x  \n", msg[0].buf);
//	printk(" msg[1].buf = %x  \n", msg[1].buf);
//
//	for (i=0; i<8;i++)
//	{
//	printk(" BUF[%d] = %x  \n", i,buf[i]);
//	}


	if (ret < 0) 
	{
		bad_data = 1;
		goto done;
	} 
	else
	{
		bad_data = 0;

        /*XY coordinate */
        tp1.y = buf[1] | ((uint16_t)buf[0] << 2); 
		tp1.x = buf[3] | ((uint16_t)buf[2] << 2);
		tp2.y = buf[5] | ((uint16_t)buf[4] << 2); 
		tp2.x = buf[7] | ((uint16_t)buf[6] << 2);

		touch = buf[8];
		gesture = buf[9];


/*
 		if (touch & 0x02)  
		{
	 		finger = 2;
        }
 		else if (touch & 0x01)  
		{
			finger = 1;
		}
        else  
		{
 			finger = 0;
 		}
*/


		if ( ((tp1.y > 0) && (tp1.x > 0)) && ((tp2.y > 0) && (tp2.x > 0)))
		{
			dbg("1---finger_count_1=%d,finger_count_2=%d\n",finger_count_1,finger_count_2);
			finger = 2;
			finger_count_2 ++;
			finger_count_1 = 0;
#ifdef SW_AVG
        	Coord_Avg(&tp1);
       		Coord_Avg(&tp2);
#endif
		}
		else if (((tp1.y > 0) && (tp1.x > 0)) || ((tp2.y > 0) && (tp2.x > 0)))
		{
			dbg("2---finger_count_1=%d,finger_count_2=%d\n",finger_count_1,finger_count_2);
			finger = 1;

			if(finger_count_2 > TWO_COUNT)				
				finger_count_1 = TWO_COUNT;
			else if (finger_count_1 > (TWO_COUNT/2))
				finger_count_1 --;
			else
				finger_count_1 = 0;

			finger_count_2 = 0;

        	if((tp2.x != 0) && (tp2.y != 0))
       		{
            	tp1.x = tp2.x;
				tp1.y = tp2.y;
				tp2.x = 0;
				tp2.y = 0;
       		}		
		}
		else 
		{
			dbg("3---finger_count_1=%d,finger_count_2=%d\n",finger_count_1,finger_count_2);
			finger_count_1 = 0;
			finger_count_2 = 0;
			finger = 0;
		}






		if(finger == 0)
		{
			//printk("======finger = 0 =====\n");
			for(i=0;i<2;i++){
				input_mt_slot(ts->input_dev, i);
				input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, false);
		        }
#if 0
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);

			input_report_abs(ts->input_dev, ABS_TOOL_WIDTH, 0); // 0 touch_width,File system do judge this register
			input_report_key(ts->input_dev, BTN_TOUCH, finger);  // finger  num   0, 1, 2
            input_report_key(ts->input_dev, BTN_2, finger > 1); // finger2 state 0, 0, 1
            input_sync(ts->input_dev);   // up load data
#endif			
		}
		else if ((finger == 1)&&(finger_count_1 == 0))
		{	
			//printk("finger == 1,finger_count_1 = 0\n");
			gADPoint.x = tp1.x;
			gADPoint.y = tp1.y;
#if 0
			X1 = (tp1.x - 40)*1024 / (934 - 40);
			Y1 = (tp1.y - 162)*600 / (931 - 162);	
#else			
			TouchPanelCalibrateAPoint(tp1.x, tp1.y, &X1, &Y1);
			X1 = X1 / 4;
			Y1 = Y1 / 4;
			X1 = (X1 > 0) ? X1 : 0;
			Y1 = (Y1 > 0) ? Y1 : 0;
			X1 = (X1 < 1023) ? X1 : 1023;
			Y1 = (Y1 < 599) ? Y1 : 599;
#endif
			//printk("finger == 1,X1=%d,Y1=%d\n",X1,Y1);
			input_mt_slot(ts->input_dev, 0);
			input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, true);
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 10);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_X, X1);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, Y1);

			input_mt_slot(ts->input_dev, 1);
			input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, false);
#if 0			
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 3);	
			input_report_abs(ts->input_dev, ABS_MT_POSITION_X, X1);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, Y1);
			input_mt_sync(ts->input_dev);

			input_report_abs(ts->input_dev, ABS_TOOL_WIDTH, 0); // 0 touch_width,File system do judge this register
			input_report_key(ts->input_dev, BTN_TOUCH, finger);  // finger  num   0, 1, 2
           		input_report_key(ts->input_dev, BTN_2, finger > 1); // finger2 state 0, 0, 1
           		input_sync(ts->input_dev);   // up load data     
#endif
		}
		else if((finger == 2)&&(finger_count_2 > TWO_COUNT))
		{
			//printk("finger == 2,finger_count_2 = 20\n");
#if 0		
			X1 = (tp1.x - 40)*1024 / (934 - 40);
			Y1 = (tp1.y - 162)*600 / (931 - 162);
				
			X2 = (tp2.x - 40)*1024 / (934 - 40);
			Y2 = (tp2.y - 162)*600 / (931 - 162);
#else
			TouchPanelCalibrateAPoint(tp1.x, tp1.y, &X1, &Y1);
			X1 = X1 / 4;
			Y1 = Y1 / 4;
			X1 = (X1 > 0) ? X1 : 0;
			Y1 = (Y1 > 0) ? Y1 : 0;
			X1 = (X1 < 1023) ? X1 : 1023;
			Y1 = (Y1 < 599) ? Y1 : 599;
			
			TouchPanelCalibrateAPoint(tp2.x, tp2.y, &X2, &Y2);
			X2 = X2 / 4;
			Y2 = Y2 / 4;
			X1 = (X1 > 0) ? X1 : 0;
			Y2 = (Y2 > 0) ? Y2 : 0;
			X2 = (X2 < 1023) ? X2 : 1023;
			Y2 = (Y2 < 599) ? Y2 : 599;
#endif
			//printk("X1=%d,Y1=%d,X2=%d,Y2=%d\n",X1,Y1,X2,Y2);
			input_mt_slot(ts->input_dev, 0);
			input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, true);
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 10);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_X, X1);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, Y1);

			input_mt_slot(ts->input_dev, 1);
			input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, true);
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 10);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_X, X2);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, Y2);
			
		
#if 0		
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 3);	
			input_report_abs(ts->input_dev, ABS_MT_POSITION_X, X1);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, Y1);
			input_mt_sync(ts->input_dev);
			
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 3);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_X, X2);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, Y2);
			input_mt_sync(ts->input_dev);

			input_report_abs(ts->input_dev, ABS_TOOL_WIDTH, 0); // 0 touch_width,File system do judge this register
			input_report_key(ts->input_dev, BTN_TOUCH, finger);  // finger  num   0, 1, 2
            input_report_key(ts->input_dev, BTN_2, finger > 1); // finger2 state 0, 0, 1
            input_sync(ts->input_dev);   // up load data                          
#endif
		}

		input_sync( ts->input_dev);

		if (finger)
	  	{
#ifdef SHOW_TOUCH_POINT			
			printk(" x1 = %3d, y1 = %3d,x2 = %3d, y2 = %3d, F1 = %d,X1= %3d, Y1 = %3d,  X2= %3d, Y2 = %3d,  \n", tp1.x, tp1.y, tp2.x, tp2.y, finger, X1, Y1,X2, Y2);
#endif
			hrtimer_start(&ts->timer, ktime_set(0, 10000000), HRTIMER_MODE_REL);			//report rate 1/10000000=100Hz
		}
		else
		{
		// Clear all Interrupt
			i2c_smbus_write_byte_data(ts->client, 0x3E, 0xFF);//delete by hjc for test 2-10
			
			hrtimer_cancel(&ts->timer);
	
			if (ts->use_irq){
				//printk("\n===enable_irq===\n");
				enable_irq(ts->irq);
			}

		if(!gpio_get_value(GPIO_IRQ)){
			printk("!GPIO_IRQ=%d\n",1-gpio_get_value(GPIO_IRQ));
			//i2c_smbus_write_byte_data(ts->client, 0x3E, 0xFF);//delete by hjc for test 2-10
		}
		}
		
	}

done:
	;

}

static enum hrtimer_restart rohm_ts_timer_func(struct hrtimer *timer)
{
	struct rohm_ts_data *ts = container_of(timer, struct rohm_ts_data, timer);
	queue_work(rohm_wq, &ts->work);
	//hrtimer_start(&ts->timer, ktime_set(0, 12500000), HRTIMER_MODE_REL);			//report rate 1/12500000=80Hz
	//printk("enter rohm_ts_timer_func\n");
	return HRTIMER_NORESTART;
}

static irqreturn_t rohm_ts_irq_handler(int irq, void *dev_id)
{
	struct rohm_ts_data *ts = dev_id;

	//printk("*****Rohm_ts_irq_handler*******\n"); 
	disable_irq_nosync(ts->irq);
	queue_work(rohm_wq, &ts->work);
	return IRQ_HANDLED;
}

static int rohm_init_panel(struct rohm_ts_data *ts)
{
	int i;
	int offset = 0;
	int block;
	int bytes;
	
#ifdef SPREADWAY_8_INCH
	//common setting 
	i2c_smbus_write_byte_data(ts->client, 0x30, 0x46);
	i2c_smbus_write_byte_data(ts->client, 0x31, 0x0D);
	i2c_smbus_write_byte_data(ts->client, 0x32, 0x67);//changed by hjc for sleep mode
	//i2c_smbus_write_byte_data(ts->client, 0x32, 0x66);//0x6e->enable INTVL	
	//timing setting
	i2c_smbus_write_byte_data(ts->client,  0x33, 0x00);//0x00->sample period 20ms	
	i2c_smbus_write_byte_data(ts->client,  0x50, 0x00);
	i2c_smbus_write_byte_data(ts->client,  0x60, 0x08);//4
	i2c_smbus_write_byte_data(ts->client,  0x61, 0x0a);//a
	i2c_smbus_write_byte_data(ts->client,  0x57, 0x04);
	//panel setting
#if 0
	i2c_smbus_write_byte_data(ts->client,  0x63, 0xEA);
	i2c_smbus_write_byte_data(ts->client,  0x64, 0x62);
	i2c_smbus_write_byte_data(ts->client,  0x34, 0x73); 
	i2c_smbus_write_byte_data(ts->client,  0x35, 0x86);
	
	i2c_smbus_write_byte_data(ts->client,  0x3A, 0x50);
	i2c_smbus_write_byte_data(ts->client,  0x3B, 0x03);
	i2c_smbus_write_byte_data(ts->client,  0x36, 0x04);
	i2c_smbus_write_byte_data(ts->client,  0x37, 0x08);
#else
	i2c_smbus_write_byte_data(ts->client,  0x63, 0x76);
		i2c_smbus_write_byte_data(ts->client,  0x64, 0x9C);
		i2c_smbus_write_byte_data(ts->client,  0x34, 0x44);
		i2c_smbus_write_byte_data(ts->client,  0x35, 0xA1);
		
	i2c_smbus_write_byte_data(ts->client,  0x3A, 0x4a);
	i2c_smbus_write_byte_data(ts->client,  0x3B, 0x03);
	i2c_smbus_write_byte_data(ts->client,  0x36, 0x04);
	i2c_smbus_write_byte_data(ts->client,  0x37, 0x08);
#endif
	//fixed value setting
	i2c_smbus_write_byte_data(ts->client,  0x52, 0x08);
	i2c_smbus_write_byte_data(ts->client,  0x56, 0x04);
	i2c_smbus_write_byte_data(ts->client,  0x62, 0x0F);
	i2c_smbus_write_byte_data(ts->client,  0x65, 0x01);
#endif


#ifdef JP_R4_3_20101028
	//common setting 
	i2c_smbus_write_byte_data(ts->client, 0x30, 0x46);
	i2c_smbus_write_byte_data(ts->client, 0x31, 0x05);
	i2c_smbus_write_byte_data(ts->client, 0x32, 0x66);//0x6e->enable INTVL	
	//timing setting
	i2c_smbus_write_byte_data(ts->client,  0x33, 0x00);//0x00->sample period 20ms	
	i2c_smbus_write_byte_data(ts->client,  0x50, 0x00);
	i2c_smbus_write_byte_data(ts->client,  0x60, 0x08);//4
	i2c_smbus_write_byte_data(ts->client,  0x61, 0x0a);//a
	i2c_smbus_write_byte_data(ts->client,  0x57, 0x04);
	//panel setting
	i2c_smbus_write_byte_data(ts->client,  0x63, 0x19);
	i2c_smbus_write_byte_data(ts->client,  0x64, 0xCC);
	i2c_smbus_write_byte_data(ts->client,  0x34, 0x2F); 
	i2c_smbus_write_byte_data(ts->client,  0x35, 0xBB); 
	i2c_smbus_write_byte_data(ts->client,  0x3A, 0x80);
	i2c_smbus_write_byte_data(ts->client,  0x3B, 0x00);
	i2c_smbus_write_byte_data(ts->client,  0x36, 0x10);
	i2c_smbus_write_byte_data(ts->client,  0x37, 0x10);
	//fixed value setting
	i2c_smbus_write_byte_data(ts->client,  0x52, 0x08);
	i2c_smbus_write_byte_data(ts->client,  0x56, 0x04);
	i2c_smbus_write_byte_data(ts->client,  0x62, 0x0F);
	i2c_smbus_write_byte_data(ts->client,  0x65, 0x01);
#endif
	//analog power on
	i2c_smbus_write_byte_data(ts->client, 0x40, 0x01);

	// Wait 100usec for Power-on
	udelay(100);

	// Beginning address setting of program memory for host download
	i2c_smbus_write_byte_data(ts->client, 0x70, 0x00);
	i2c_smbus_write_byte_data(ts->client, 0x71, 0x00);

	// Download firmware to BU21020
	printk("BU21023 firmware download starts!\n");

	block = CODESIZE / 32;
	bytes = CODESIZE % 32;
	
	for(i = 0; i < block; i++)
	{
		offset = i * 32;
		i2c_smbus_write_i2c_block_data(ts->client, 0x72, 32, &program[offset]);
	}

	offset = block * 32;
	for(i = 0; i < bytes; i++)
	{
		i2c_smbus_write_byte_data(ts->client, 0x72, program[offset + i]);
	}



	printk("BU21020 firmware download is completed!\n");
	// Download path setting

	i2c_smbus_write_byte_data(ts->client, 0x3E, 0xFF);
	


	// Coordinate Offset, MAF, CAF
	//i2c_smbus_write_byte_data(ts->client,  0x60, 0x00);
#ifdef FW_GESTURE
	i2c_smbus_write_byte_data(ts->client,  0x3B, 0x03);
#else
	i2c_smbus_write_byte_data(ts->client,  0x3B, 0x07);
#endif 
	// CPU power on
	i2c_smbus_write_byte_data(ts->client, 0x40, 0x03);
	// Clear all Interrupt
	i2c_smbus_write_byte_data(ts->client, 0x3D, 0xFE);
	printk("BU21023 reg0x2A = 0x%x\n",i2c_smbus_read_byte_data(ts->client, 0x2A));
	i2c_smbus_write_byte_data(ts->client, 0x3E, 0xFF);


	for (i=0;i<15;i++){
		udelay(1000);
	}

	printk("BU21023 reg0x63 = 0x%x\n",i2c_smbus_read_byte_data(ts->client, 0x63));
	printk("BU21023 reg0x64 = 0x%x\n",i2c_smbus_read_byte_data(ts->client, 0x64));
	printk("BU21023 reg0x34 = 0x%x\n",i2c_smbus_read_byte_data(ts->client, 0x34));
	printk("BU21023 reg0x35 = 0x%x\n",i2c_smbus_read_byte_data(ts->client, 0x35));
	printk("BU21023 reg0x3A = 0x%x\n",i2c_smbus_read_byte_data(ts->client, 0x3A));
	printk("BU21023 reg0x56 = 0x%x\n",i2c_smbus_read_byte_data(ts->client, 0x56));
	printk("BU21023 reg0x36 = 0x%x\n",i2c_smbus_read_byte_data(ts->client, 0x36));
	printk("BU21023 reg0x37 = 0x%x\n",i2c_smbus_read_byte_data(ts->client, 0x37));

	printk("BU21023 reg0x18 = 0x%x\n",i2c_smbus_read_byte_data(ts->client, 0x18));
	printk("BU21023 reg0x65 = 0x%x\n",i2c_smbus_read_byte_data(ts->client, 0x65));
	printk("BU21023 reg0x6B = 0x%x\n",i2c_smbus_read_byte_data(ts->client, 0x6B));
	printk("BU21023 reg0x6C = 0x%x\n",i2c_smbus_read_byte_data(ts->client, 0x6C));

 	i2c_smbus_write_byte_data(ts->client,	0x65, 0x11);

	printk("BU21023 reg0x65 = 0x%x\n",i2c_smbus_read_byte_data(ts->client, 0x65));
	printk("BU21023 reg0x6B = 0x%x\n",i2c_smbus_read_byte_data(ts->client, 0x6B));
	printk("BU21023 reg0x6C = 0x%x\n",i2c_smbus_read_byte_data(ts->client, 0x6C));

	i2c_smbus_write_byte_data(ts->client,	0x65, 0x01);
	// Clear all Interrupt
	printk("BU21023 reg0x2A = 0x%x\n",i2c_smbus_read_byte_data(ts->client, 0x2A));
	i2c_smbus_write_byte_data(ts->client, 0x3E, 0xFF);
	// Init end
	////////////////////////////////////////////////////////////////////////

	return 0;
}


#ifdef CONFIG_HAS_EARLYSUSPEND
static void rohm_early_suspend(struct early_suspend *handler)
{
	struct rohm_ts_data *ts = container_of(handler, struct rohm_ts_data, early_suspend);

	printk("%s\n", __FUNCTION__);

	i2c_smbus_write_byte_data(ts->client, 0x40, 0x02);
	udelay(100);
	i2c_smbus_write_byte_data(ts->client, 0x3E, 0xFF);

	cancel_work_sync(&ts->work);
	disable_irq_nosync(ts->irq);

//	if(ts->hw_power)
//		ts->hw_power(0);
}

static void rohm_early_resume(struct early_suspend *handler)
{
	struct rohm_ts_data *ts = container_of(handler, struct rohm_ts_data, early_suspend);

	printk("%s\n", __FUNCTION__);
/*
	i2c_smbus_write_byte_data(ts->client, 0x40, 0x02);
	udelay(100);
	i2c_smbus_write_byte_data(ts->client, 0x3E, 0xFF);
*/	
	i2c_smbus_write_byte_data(ts->client, 0x40, 0x03);
	udelay(100);
	i2c_smbus_write_byte_data(ts->client, 0x3E, 0xFF);
	
	enable_irq(ts->irq);
}
#endif
/*********************for test**************************/

static ssize_t touch_mode_show(struct class *cls,struct class_attribute *attr, char *_buf)
{
	printk("%s\n",__FUNCTION__); 
	read_from_ic();
	//hrtimer_start(&TS->timer, ktime_set(0, 10000000), HRTIMER_MODE_REL);
}

static ssize_t touch_mode_store(struct class *cls,struct class_attribute *attr, char *_buf,size_t _count)
{
	printk("%s\n",__FUNCTION__);
        return _count;
} 

/**********************for test********************/

static struct class *tp_class = NULL;
static CLASS_ATTR(touchcalibration, 0666, touch_mode_show, touch_mode_store);


static int rohm_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct rohm_ts_data *ts;
	int ret = 0;
	struct rohm_platform_data *pdata;
	int error;
    printk(KERN_ERR "ROHM BU21018 rohm_ts_probe!!\n");
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) 
	{
		printk(KERN_ERR "Rohm_ts_probe: need I2C_FUNC_I2C\n");
		ret = -ENODEV;
		goto err_check_functionality_failed;
	}

	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts == NULL) 
	{
		ret = -ENOMEM;
		goto err_alloc_data_failed;
	}
	INIT_WORK(&ts->work, rohm_ts_work_func);
	ts->client = client;
	i2c_set_clientdata(client, ts);
	pdata = client->dev.platform_data;

	if (pdata){
		ts->hw_init = pdata->hw_init;
		ts->hw_power = pdata->hw_power;
		ts->hw_reset = pdata->hw_reset;
	}

	if(ts->hw_init) 
		ts->hw_init();

	if(ts->hw_power)
		ts->hw_power(1);

	if(ts->hw_reset)
		ts->hw_reset();

    //Download 

    ////////////////////////////////////////////////////////////////////////
    // Init BU21020
    ////////////////////////////////////////////////////////////////////////

	//Reset ,modified by tracy, 2010-08-31
	//gpio_set_value(OMAP3_DEVKIT_TS_RSTB, 0);
    //udelay(200);
	//gpio_set_value(OMAP3_DEVKIT_TS_RSTB, 1);
	// Wait 200usec for Reset
   //udelay(200);
#if 0   
    gpio_direction_output(RK29_PIN0_PD7, 0);
    msleep(30);
    gpio_set_value(RK29_PIN0_PD7,GPIO_HIGH);

    gpio_direction_output(RK29_PIN6_PC3, 0);
    msleep(30);
    gpio_set_value(RK29_PIN6_PC3,GPIO_LOW);
    msleep(30);
    gpio_set_value(RK29_PIN6_PC3,GPIO_HIGH);
    mdelay(200);

#endif

    ////////////////////////////////////////////////////////////////////////
    // Init BU21023
    ////////////////////////////////////////////////////////////////////////
	// Wait 200usec for Reset
    //udelay(200);

	rohm_init_panel(ts); 

	ts->input_dev = input_allocate_device();
	if (ts->input_dev == NULL) 
	{
		ret = -ENOMEM;
		printk(KERN_ERR "Rohm_ts_probe: Failed to allocate input device\n");
		goto err_input_dev_alloc_failed;
	}
	ts->input_dev->name = "Rohm-CTP-BUMV21018MWV";
#if 0
	set_bit(EV_SYN, ts->input_dev->evbit);
	set_bit(EV_KEY, ts->input_dev->evbit);
	set_bit(BTN_TOUCH, ts->input_dev->keybit);
	set_bit(BTN_2, ts->input_dev->keybit);
	set_bit(EV_ABS, ts->input_dev->evbit);
	printk("Rohm_ts_probe: --------------------setting ready\n");



	////////////////////////////////////////////////////////////////////////////
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, 1023, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, 599, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
//	input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0, 15, 0, 0);
	////////////////////////////////////////////////////////////////////////////
	input_set_abs_params(ts->input_dev, ABS_X,0, 1023, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_Y,0, 599, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_PRESSURE, 0, 1550, 0, 0);  //1550  two point
	input_set_abs_params(ts->input_dev, ABS_TOOL_WIDTH, 0, 15, 0, 0); 	//15   two point
	input_set_abs_params(ts->input_dev, ABS_HAT0X, 0 , 1023, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_HAT0Y, 0, 599, 0, 0);
#endif

#if 1
	__set_bit(INPUT_PROP_DIRECT, ts->input_dev->propbit);
	__set_bit(EV_ABS, ts->input_dev->evbit);
	
	input_mt_init_slots(ts->input_dev, MAX_SUPPORT_POINT);//for 4.0 hjc
	input_set_abs_params( ts->input_dev, ABS_X, 0, 1025, 0, 0);
	input_set_abs_params( ts->input_dev, ABS_Y, 0, 601, 0, 0);

	input_set_abs_params( ts->input_dev, ABS_MT_POSITION_X, 0, 1025, 0, 0);
	input_set_abs_params( ts->input_dev, ABS_MT_POSITION_Y, 0, 601, 0, 0);
        input_set_abs_params( ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
#endif

	ret = input_register_device(ts->input_dev);
	if(ret) 
	{
		printk(KERN_ERR "==>: Unable to register %s input device\n", ts->input_dev->name);
		goto err_input_register_device_failed;
	}

	ts->irq = gpio_to_irq(client->irq);
	GPIO_IRQ = client->irq;
	if(ts->irq) 
	{
		ret = request_irq(ts->irq, rohm_ts_irq_handler, IRQF_TRIGGER_LOW, client->name, ts);  	
//Trigger status used IRQF_TRIGGER_FALLING ; ref \linux\interrupt.h
// IRQF_TRIGGER_NONE	
// IRQF_TRIGGER_RISING	
// IRQF_TRIGGER_FALLING	
// IRQF_TRIGGER_HIGH
// IRQF_TRIGGER_LOW
// IRQF_TRIGGER_PROBE
/////////////////////////////

		printk("Request IRQ Failed==>ret : %d\n", ret);
		if (ret == 0) 
		{
			ts->use_irq = 1;	//1 : interrupt mode/0 : polling mode
			//free_irq(ts->irq, ts);
		}
		else 
		{
			ts->use_irq = 0;	//1 set 1 : interrupt mode/0 : polling mode
			//dev_err(&client->dev, "request_irq failed\n");
		}	
	}
	if (!ts->use_irq) 
	{
		hrtimer_init(&ts->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		ts->timer.function = rohm_ts_timer_func;
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
	}
		//timer init
	hrtimer_init(&ts->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	ts->timer.function = rohm_ts_timer_func;

	printk("==>:Touchscreen Up %s in %s mode\n", ts->input_dev->name, ts->use_irq ? "interrupt" : "polling");

	gADPoint.x = 0;
	gADPoint.y = 0;

	tp_calib_iface_init(screen_x,screen_y,uncali_x_default,uncali_y_default);

#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;
	ts->early_suspend.suspend = rohm_early_suspend;
	ts->early_suspend.resume = rohm_early_resume;
	register_early_suspend(&ts->early_suspend);
	printk("Rohm TP: Register early_suspend done\n");
#endif
        TS =  ts;
        tp_class = class_create(THIS_MODULE, "touchpanel_for_test");
    if (IS_ERR(tp_class))
        {
            printk("Create class touchpanel failed.\n");
            error = -ENOMEM;
            goto err_free_mem;
        }
        error = class_create_file(tp_class,&class_attr_touchcalibration);
        printk("Create class touchpanel is sucess\n");  

	return 0;

err_free_mem:
        input_free_device(ts->input_dev);
        kfree(ts);

	
	printk("%s,probe touchplus ok!\n",__FUNCTION__);
	return 0;
err_input_register_device_failed:
	input_free_device(ts->input_dev);

err_input_dev_alloc_failed:
	//err_detect_failed:
err_power_failed:
	kfree(ts);
err_alloc_data_failed:
err_check_functionality_failed:
	return ret;
}

static int rohm_ts_remove(struct i2c_client *client)
{
	struct rohm_ts_data *ts = i2c_get_clientdata(client);
	if (ts->use_irq)
		free_irq(ts->irq, ts);
	else
		hrtimer_cancel(&ts->timer);
	input_unregister_device(ts->input_dev);
	kfree(ts);
	return 0;
}

static const struct i2c_device_id rohm_ts_id[] = 
{
	{ "rohm-i2c", 0 },
	{ }
};

static struct i2c_driver rohm_ts_driver = 
{
	.probe		= rohm_ts_probe,
	.remove		= rohm_ts_remove,
	.id_table	= rohm_ts_id,
	.driver = {
		.name	= "rohm-i2c",
	},
};

static int __devinit rohm_ts_init(void)
{
	//printk(KERN_ERR "ROHM BU21018 rohm_ts_init \n");
	rohm_wq = create_singlethread_workqueue("rohm_wq");
	if (!rohm_wq)
	{
		printk(KERN_ERR "BU21018 create_singlethread_workqueue ERROR!!\n");
		return -ENOMEM;
	}else
		{
		  printk("BU21018 Create_singlethread_workqueue is ok\n");
		}	
	return i2c_add_driver(&rohm_ts_driver);
}

static void __exit rohm_ts_exit(void)
{
	i2c_del_driver(&rohm_ts_driver);
	if (rohm_wq)
		destroy_workqueue(rohm_wq);
}

module_init(rohm_ts_init);
module_exit(rohm_ts_exit);

MODULE_DESCRIPTION("Rohm Touchscreen Driver");
MODULE_LICENSE("GPL");
