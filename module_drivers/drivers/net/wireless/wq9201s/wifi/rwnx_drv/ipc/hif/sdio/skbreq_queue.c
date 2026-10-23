#include "sdio.h"

#include "wq_log.h"

static inline int wq_skbreq_init(struct wq_skbreq *req, struct wq_sdio *unused)
{
	(void)unused;
	return 0;
}

#ifdef SDIO_RX_AGGR_MODE
static inline int wq_skbreq_aggrin_init(struct wq_skbreq *req,
					struct wq_sdio *wq_sdio)
{
	req->skb = __dev_alloc_skb(SDIO_ADMA_RX_LEN_MAX, GFP_KERNEL);
	if (req->skb) {
		return 0;
	} else {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: __dev_alloc_skb failed\n",
		       __func__);
		return -ENOMEM;
	}
}
#endif

#ifdef SDIO_TX_AGGR_MODE
static inline int wq_skbreq_aggrout_init(struct wq_skbreq *req,
					 struct wq_sdio *wq_sdio)
{
	req->skb = __dev_alloc_skb(SDIO_ADMA_WLAN_TX_LEN_MAX, GFP_KERNEL);
	if (req->skb) {
		return 0;
	} else {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: __dev_alloc_skb failed\n",
		       __func__);
		return -ENOMEM;
	}
}
#endif

static inline void wq_skbreq_deinit(struct wq_skbreq *req, void *unused)
{
	struct sk_buff *skb = req->skb;

	(void)unused;
	if (skb) {
		req->skb = NULL;
		dev_kfree_skb_any(skb);
	}
}

static WQ_LIST_POOL_INIT_FN(wq_skbreq_pool_init, struct wq_skbreq, list,
			    struct wq_sdio *);
static WQ_LIST_POOL_DEINIT_FN(wq_skbreq_pool_deinit, struct wq_skbreq, list,
			      void *);

int wq_sdio_pools_init(struct wq_sdio *wq_sdio)
{
	int ret;
	const char *pool;

	ret = wq_skbreq_pool_init(&wq_sdio->pools.pktout, WQ_PTKOUT_NUM,
				  wq_skbreq_init, NULL);
	if (ret < 0) {
		pool = "pktout";
		goto fail;
	}

	ret = wq_skbreq_pool_init(&wq_sdio->pools.msgout, WQ_MSGOUT_NUM,
				  wq_skbreq_init, NULL);
	if (ret < 0) {
		pool = "msgout";
		goto fail;
	}

#ifdef SDIO_RX_AGGR_MODE
	ret = wq_skbreq_pool_init(&wq_sdio->pools.aggrin, WQ_AGGIN_NUM,
				  wq_skbreq_aggrin_init, wq_sdio);
	if (ret < 0) {
		pool = "aggrin";
		goto fail;
	}
#endif

#ifdef SDIO_TX_AGGR_MODE
	ret = wq_skbreq_pool_init(&wq_sdio->pools.aggrout, WQ_AGGROUT_NUM,
				  wq_skbreq_aggrout_init, wq_sdio);
	if (ret < 0) {
		pool = "aggrout";
		goto fail;
	}
#endif

	(void)pool;

	return 0;

fail:
	WQ_DBG(DM_TRBUS, DL_ERR, "%s: init pool %s failed\n", __func__, pool);

	return ret;
}

void wq_sdio_pools_deinit(struct wq_sdio *wq_sdio)
{
	wq_skbreq_pool_deinit(&wq_sdio->pools.msgout, wq_skbreq_deinit, NULL);
	wq_skbreq_pool_deinit(&wq_sdio->pools.pktout, wq_skbreq_deinit, NULL);

#ifdef SDIO_RX_AGGR_MODE
	wq_skbreq_pool_deinit(&wq_sdio->pools.aggrin, wq_skbreq_deinit, NULL);
#endif

#ifdef SDIO_TX_AGGR_MODE
	wq_skbreq_pool_deinit(&wq_sdio->pools.aggrout, wq_skbreq_deinit, NULL);
#endif
}
