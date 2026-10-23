#ifndef WQ_API_VERSION_H_
#define WQ_API_VERSION_H_

/*
 * host should make sure its own API version is same as firmware's.
 * otherwise, struct definition may be difference, and unpredicted things happen.
 */
#define MK_WQ_API_VERSION(a,b,c,d)     ((((a) & 0x7f) << 25) | (((b) & 0x1f) << 20) |  \
                                        (((c) & 0xff) << 12) | (((d) & 0xfff) << 0))

#define WQ_API_VERSION MK_WQ_API_VERSION(2, 2, 1, 685)

#endif /* WQ_API_VERSION_H_ */
