/* タイマ関係 */

#include "bootpack.h"

#define PIT_CTRL	0x0043
#define PIT_CNT0	0x0040

struct TIMERCTL timerctl;

#define TIMER_FLAGS_ALLOC		1	/* 確保した状態 */
#define TIMER_FLAGS_USING		2	/* タイマ作動中 */

void init_pit(void)
{
	int i;
	io_out8(PIT_CTRL, 0x34);
	io_out8(PIT_CNT0, 0x9c);
	io_out8(PIT_CNT0, 0x2e);
	timerctl.count = 0;
	timerctl.next_timer = 0xffffffff;/* 作動中のタイマがない */
	timerctl.using = 0;
	for (i = 0; i < MAX_TIMER; i++) {
		timerctl.timers0[i].flags = 0; /* 未使用 */
	}
	return;
}

struct TIMER *timer_alloc(void)
{
	int i;
	for (i = 0; i < MAX_TIMER; i++) {
		if (timerctl.timers0[i].flags == 0) {
			timerctl.timers0[i].flags = TIMER_FLAGS_ALLOC;
			return &timerctl.timers0[i];
		}
	}
	return 0; /* 見つからなかった */
}

void timer_free(struct TIMER *timer)
{
	timer->flags = 0; /* 未使用 */
	return;
}

void timer_init(struct TIMER *timer, struct FIFO32 *fifo,int data)
{
	timer->fifo = fifo;
	timer->data = data;
	return;
}

void timer_settime(struct TIMER *timer, unsigned int timeout)
{
	int e;
	struct TIMER *t, *s;
	timer->timeout = timeout + timerctl.count;
	timer->flags = TIMER_FLAGS_USING;
	/* 割り込みを禁止 */
	e = io_load_eflags();
	io_cli();
	timerctl.using++;
	if (timerctl.using == 1) {
		/* 動作中のタイマがこれ1つになる場合 */
		timerctl.timers[0] = timer;
		timer->next = 0; /* 次は存在しない */
		timerctl.next_timer = timer->timeout;
		io_store_eflags(e);
		return;
	}
	t = timerctl.timers[0];
	if (timer->timeout <= t->timeout) {
		/* 先頭に入れる場合 */
		timerctl.timers[0] = timer;
		timer->next = t; /* 次はt(timers[0]) */
		timerctl.next_timer = timer->timeout;
		io_store_eflags(e);
		return;
	}
	/* どこに入れればいいかを探す */
	for (;;) {
		s = t;
		t = t->next;
		if (t==0) {
			break; /* 一番うしろ */
		}
		if (timer->timeout <= t->timeout) {
			/* sとtの間に入れる */
			s->next = timer; /* sの次はtimer */
			timer->next = t; /* timerの次はt */
			io_store_eflags(e);
			return;
		}
	}
	/* 一番最後に入れる場合 */
	s->next = timer;
	timer->next = 0;
	io_store_eflags(e);
	return;
}

void inthandler20(int *esp)
{
	int i, j;
	struct TIMER *timer;
	io_out8(PIC0_OCW2, 0x60);	/* IRQ-00受付完了をPICに通知 */
	timerctl.count++;
	if (timerctl.next_timer > timerctl.count) {
		return; /* まだ次の時刻になっていないため、この時点で打ち切り */
	}
	timer = timerctl.timers[0]; /* とりあえず先頭の番地をtimerに代入 */
	for (i = 0; i<timerctl.using; i++) {
		/* timersのタイマは全て動作中のものなので、flagsを確認しない */
		if (timerctl.timers[i]->timeout > timerctl.count) {
			break;
		}
		/* タイムアウト */
		timer->flags = TIMER_FLAGS_ALLOC;
		fifo32_put(timerctl.timers[i]->fifo, timerctl.timers[i]->data);
		timer = timer->next; /* 次のタイマの番地をtimerに代入 */
	}
	/* リスト構造を利用したずらし */
	timerctl.timers[0] = timer;
	/* timerctl.next_timerの設定 */
	if (timerctl.using > 0) {
		timerctl.next_timer = timerctl.timers[0]->timeout;
	} else {
		timerctl.next_timer = 0xffffffff;
	}
	return;
}
