/*---
flags: [mbjs:track-promise-rejections]
---*/

Promise.reject().catch(() => print('oops'))
Promise.resolve().then(() => print('ok'))
