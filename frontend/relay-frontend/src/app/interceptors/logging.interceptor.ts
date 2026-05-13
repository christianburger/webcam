import { HttpInterceptorFn, HttpResponse } from '@angular/common/http';
import { tap, catchError } from 'rxjs/operators';
import { throwError } from 'rxjs';

export const loggingInterceptor: HttpInterceptorFn = (req, next) => {
  const t0 = Date.now();
  console.groupCollapsed(`[HTTP ▶] ${req.method} ${req.url}`);
  console.log('headers:', req.headers.keys());
  if (req.body) console.log('body:', req.body);
  console.groupEnd();

  return next(req).pipe(
    tap(event => {
      if (event instanceof HttpResponse) {
        const ms = Date.now() - t0;
        const body = event.body;
        const preview =
          body && typeof body === 'object' && 'base64' in (body as object)
            ? `{ base64: <${((body as any).base64 as string)?.length ?? 0} chars>, contentType: ${(body as any).contentType} }`
            : JSON.stringify(body)?.slice(0, 200);
        console.groupCollapsed(`[HTTP ◀] ${event.status} ${req.url} (${ms}ms)`);
        console.log('body:', preview);
        console.groupEnd();
      }
    }),
    catchError(err => {
      const ms = Date.now() - t0;
      console.group(`[HTTP ✗] ${err.status ?? '?'} ${req.url} (${ms}ms)`);
      console.error('status:', err.status, err.statusText);
      console.error('message:', err.message);
      if (err.error) console.error('error body:', err.error);
      console.groupEnd();
      return throwError(() => err);
    })
  );
};
