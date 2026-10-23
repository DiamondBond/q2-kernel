#include "usb.h"

#include "wq_log.h"

static inline int wq_urb_ctx_init(struct wq_urb_ctx *req, struct wq_usb *wq_usb)
{
	req->wq_usb = wq_usb;
	req->urb = usb_alloc_urb(0, GFP_ATOMIC);
	return 0;
}

static inline void wq_urb_ctx_deinit(struct wq_urb_ctx *req, void *unused)
{
	(void)unused;
	usb_free_urb(req->urb);
}

static inline int wq_urb_ctx_skb_alloc(struct wq_list_pool *pool, u32 skb_size)
{
	u32 i;
	struct wq_urb_ctx *ctx = pool->entries;

	WQ_DBG(DM_TRBUS, DL_WRN, "%s: list_num=%d, skb_size=%d\n", __func__,
	       pool->num, skb_size);

	for (i = 0; i < pool->num; i++) {
		BUG_ON(!ctx);

		ctx->skb = dev_alloc_skb(skb_size);
		if (!ctx->skb) {
			return -ENOMEM;
		}
		ctx++;
	}
	return 0;
}

static inline void wq_urb_ctx_skb_free(struct wq_list_pool *pool)
{
	u32 i;
	struct wq_urb_ctx *ctx = pool->entries;

	if (!ctx) {
		return;
	}

	for (i = 0; i < pool->num; i++) {
		if (ctx->skb) {
			dev_kfree_skb_any(ctx->skb);
			ctx->skb = NULL;
		}
		ctx++;
	}
}

static WQ_LIST_POOL_INIT_FN(wq_urb_ctx_pool_init, struct wq_urb_ctx, list,
			    struct wq_usb *);
static WQ_LIST_POOL_DEINIT_FN(wq_urb_ctx_pool_deinit, struct wq_urb_ctx, list,
			      void *);

int wq_usb_pools_init(struct wq_usb *wq_usb)
{
	int ret;
	const char *pool;

	ret = wq_urb_ctx_pool_init(&wq_usb->pools.msgin, WQ_MSGIN_URB_NUM,
				   wq_urb_ctx_init, wq_usb);
	if (ret < 0) {
		pool = "msgin";
		goto fail;
	}
	ret = wq_urb_ctx_pool_init(&wq_usb->pools.msgout, WQ_MSGOUT_URB_NUM,
				   wq_urb_ctx_init, wq_usb);
	if (ret < 0) {
		pool = "msgout";
		goto fail;
	}
	ret = wq_urb_ctx_pool_init(&wq_usb->pools.pktin, WQ_PKTIN_URB_NUM,
				   wq_urb_ctx_init, wq_usb);
	if (ret < 0) {
		pool = "pktin";
		goto fail;
	}
	ret = wq_urb_ctx_pool_init(&wq_usb->pools.pktout, WQ_PKTOUT_URB_NUM,
				   wq_urb_ctx_init, wq_usb);
	if (ret < 0) {
		pool = "pktout";
		goto fail;
	}

	ret = wq_urb_ctx_skb_alloc(&wq_usb->pools.pktin,
				   WQ_USB_MTU_PKT_BUNDLE_I);
	if (ret < 0) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "[auto]msg:%s: pool pktin alloc skb failed (%d)\n",
		       __func__, WQ_USB_MTU_PKT_BUNDLE_I);
		return ret;
	}

	(void)pool;

	return 0;

fail:
	WQ_DBG(DM_TRBUS, DL_ERR, "%s: init pool %s failed\n", __func__, pool);

	return ret;
}

void wq_usb_pools_deinit(struct wq_usb *wq_usb)
{
	wq_urb_ctx_skb_free(&wq_usb->pools.pktin);
	wq_urb_ctx_pool_deinit(&wq_usb->pools.msgin, wq_urb_ctx_deinit, NULL);
	wq_urb_ctx_pool_deinit(&wq_usb->pools.msgout, wq_urb_ctx_deinit, NULL);
	wq_urb_ctx_pool_deinit(&wq_usb->pools.pktin, wq_urb_ctx_deinit, NULL);
	wq_urb_ctx_pool_deinit(&wq_usb->pools.pktout, wq_urb_ctx_deinit, NULL);
}
