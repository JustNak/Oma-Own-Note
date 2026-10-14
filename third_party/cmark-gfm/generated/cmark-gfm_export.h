#ifndef CMARK_GFM_EXPORT_H
#define CMARK_GFM_EXPORT_H

#ifdef CMARK_GFM_STATIC_DEFINE
#define CMARK_GFM_EXPORT
#define CMARK_GFM_NO_EXPORT
#else
#define CMARK_GFM_EXPORT __attribute__((visibility("default")))
#define CMARK_GFM_NO_EXPORT __attribute__((visibility("hidden")))
#endif

#endif
