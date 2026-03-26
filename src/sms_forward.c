/*
 * Copyright (c) 2022 OpenLuat & AirM2M
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */


#include "common_api.h"
#include "luat_rtos.h"
#include "luat_debug.h"
#include "netdb.h"
#include "lwip/ip4_addr.h"
#include "luat_mobile.h"
#include "luat_sms.h"
#include "luat_mem.h"
#include "smtp.h"
#include "sms_forward_config.h"
#include "unicode.h"
#include <string.h>

luat_rtos_task_handle sms_proc_task_handle;

extern void luat_sms_proc(uint32_t event, void *param);

typedef struct
{
	uint8_t is_link_up;
}demo_ctrl_t;

static demo_ctrl_t g_s_demo;


static void sms_recv_cb(uint8_t event,void *param)
{
	LUAT_DEBUG_PRINT("event:[%d]", event);
	LUAT_SMS_RECV_MSG_T *sms_data = NULL;
    sms_data = (LUAT_SMS_RECV_MSG_T *)malloc(sizeof(LUAT_SMS_RECV_MSG_T));
    memset(sms_data, 0x00, sizeof(LUAT_SMS_RECV_MSG_T));
    memcpy(sms_data, (LUAT_SMS_RECV_MSG_T *)param, sizeof(LUAT_SMS_RECV_MSG_T));
    int ret = luat_rtos_message_send(sms_proc_task_handle, 0, sms_data);
	if(ret != 0)
	{
		LUAT_MEM_FREE(sms_data);
		sms_data = NULL;
	}
}



static void demo_init_sms()
{
	//初始化SMS, 初始化必须在最开始调用
	luat_sms_init();
    luat_sms_recv_msg_register_handler(sms_recv_cb);

}

void get_my_number(void) {
    char msisdn[32] = {0};
    // 0 代表第一个 SIM 卡槽
    int ret = luat_mobile_get_msisdn(0, msisdn, sizeof(msisdn));
    
    if (ret > 0) {
        LUAT_DEBUG_PRINT("本机号码: %s", msisdn);
    } else {
        // 如果返回 0 或负数，说明 SIM 卡内没有记录号码
        LUAT_DEBUG_PRINT("无法从 SIM 卡读取号码 (ErrorCode: %d)", ret);
    }
}

static void forward_sms_task(void *param)
{
	uint32_t message_id = 0;
	LUAT_SMS_RECV_MSG_T *data = NULL;



	char timestamp[32] = {0};
	char msg_seg[5][300];
	char msg[1024] = {0};
	uint32_t uni_hex[256] = {0};
	uint8_t rcv_seg = 0;
	char *mail_body = &msg_seg;
	
	get_my_number();

	while(1)
	{
		if(luat_rtos_message_recv(sms_proc_task_handle, &message_id, (void **)&data, LUAT_WAIT_FOREVER) == 0)
		{
	        LUAT_DEBUG_PRINT("Dcs:[%d]", data->dcs_info.alpha_bet);
	        LUAT_DEBUG_PRINT("Time:[\"%02d/%02d/%02d,%02d:%02d:%02d %c%02d\"]", data->time.year, data->time.month, data->time.day, data->time.hour, data->time.minute, data->time.second,data->time.tz_sign, data->time.tz);
	        LUAT_DEBUG_PRINT("Phone:[%s]", data->phone_address);
	        LUAT_DEBUG_PRINT("ScAddr:[%s]", data->sc_address);
	        LUAT_DEBUG_PRINT("PDU len:[%d]", data->sms_length);
	        LUAT_DEBUG_PRINT("PDU: [%s]", data->sms_buffer);
			LUAT_DEBUG_PRINT("refNum: [%d] maxNum :[%d] seqNum [%d]", data->refNum, data->maxNum, data->seqNum);

			rcv_seg++;

			if(data->dcs_info.alpha_bet == 2) {
				uni_str_to_hex(data->sms_buffer, uni_hex, data->sms_length);
				uni_hex_to_utf8(uni_hex, &msg_seg[data->seqNum > 0 ? data->seqNum - 1 : 0],  data->sms_length / 4);
			} else {
				memcpy(&msg_seg[data->seqNum > 0 ? data->seqNum - 1 : 0], data->sms_buffer, data->sms_length + 1);
			}

			// 长短信最后一包 或 非长短信
			if(data->maxNum <= rcv_seg) {
				memset(msg, 0, sizeof(msg));
				LUAT_DEBUG_PRINT(" rcv last segment %d", rcv_seg);
				// 拼接短信

				for(int i = 0; i < rcv_seg; i++){
					LUAT_DEBUG_PRINT("msg seg[%d] : %s", i, msg_seg[i]);
					strcat(msg, msg_seg[i]);
				}
				LUAT_DEBUG_PRINT("msg utf8 : %s", msg);

				snprintf(timestamp, 32, "[%02d年%02d月%02d日,%02d:%02d:%02d %c%02d]", data->time.year, data->time.month, data->time.day, data->time.hour, data->time.minute, data->time.second,data->time.tz_sign, data->time.tz);
				LUAT_DEBUG_PRINT("timestamp %s", timestamp);

				snprintf(mail_body, SMTP_BUF_SIZE, "From:\"SMS\"<%s>\r\nTo:%s\r\nsubject: New SMS!\r\n\r\n %s 收到来自%s的新消息:\n %s", 
						MAIL_USER, MAIL_TO, timestamp,
						data->phone_address, msg);
				LUAT_DEBUG_PRINT(" send_email %s", mail_body);
				send_email(MAIL_USER, MAIL_PASS, MAIL_TO, mail_body);
				rcv_seg = 0;
				*mail_body = '\0';
			}

			LUAT_MEM_FREE(data);

			data = NULL;
        }
	}
}


static void mobile_event_cb(LUAT_MOBILE_EVENT_E event, uint8_t index, uint8_t status)
{
	switch(event)
	{
	case LUAT_MOBILE_EVENT_CFUN:
		LUAT_DEBUG_PRINT("CFUN消息，status %d", status);
		break;
	case LUAT_MOBILE_EVENT_SIM:
		LUAT_DEBUG_PRINT("SIM卡消息");
		switch(status)
		{
		case LUAT_MOBILE_SIM_READY:
			LUAT_DEBUG_PRINT("SIM卡正常工作");
			break;
		case LUAT_MOBILE_NO_SIM:
			LUAT_DEBUG_PRINT("SIM卡不存在");
			break;
		case LUAT_MOBILE_SIM_NEED_PIN:
			LUAT_DEBUG_PRINT("SIM卡需要输入PIN码");
			break;
		}
		break;
	case LUAT_MOBILE_EVENT_REGISTER_STATUS:
		LUAT_DEBUG_PRINT("移动网络服务状态变更，当前为%d", status);
		break;
	case LUAT_MOBILE_EVENT_CELL_INFO:
		switch(status)
		{
		case LUAT_MOBILE_CELL_INFO_UPDATE:
			break;
		case LUAT_MOBILE_SIGNAL_UPDATE:
			break;
		}
		break;
	case LUAT_MOBILE_EVENT_PDP:
		LUAT_DEBUG_PRINT("CID %d PDP激活状态变更为 %d", index, status);
		break;
	case LUAT_MOBILE_EVENT_NETIF:
		LUAT_DEBUG_PRINT("internet工作状态变更为 %d", status);
		switch (status)
		{
		case LUAT_MOBILE_NETIF_LINK_ON:
			LUAT_DEBUG_PRINT("可以上网");
			g_s_demo.is_link_up = 1;
			break;
		default:
			LUAT_DEBUG_PRINT("不能上网");
			g_s_demo.is_link_up = 0;
			break;
		}
		break;
	case LUAT_MOBILE_EVENT_TIME_SYNC:
		LUAT_DEBUG_PRINT("通过移动网络同步了UTC时间");
		break;
	case LUAT_MOBILE_EVENT_CSCON:
		LUAT_DEBUG_PRINT("RRC状态 %d", status);
		break;
	default:
		break;
	}
}

static void task_SMS_forward_init(void)
{
	luat_mobile_event_register_handler(mobile_event_cb);

	demo_init_sms();
	luat_rtos_task_create(&sms_proc_task_handle, 10*1024, 50, "forward_sms_task", forward_sms_task, NULL, 50);
}


INIT_TASK_EXPORT(task_SMS_forward_init, "1");