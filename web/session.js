(() => {
  'use strict';

  const sessionKey = 'cad-policy-studio.session-id';
  const tokenKey = 'cad-policy-studio.api-token';

  function randomId() {
    if (globalThis.crypto?.randomUUID) return globalThis.crypto.randomUUID();
    if (globalThis.crypto?.getRandomValues) {
      const bytes = new Uint8Array(16);
      globalThis.crypto.getRandomValues(bytes);
      return Array.from(bytes, b => b.toString(16).padStart(2, '0')).join('');
    }
    return `session-${Date.now()}-${Math.random().toString(36).slice(2)}`;
  }

  let sessionId = sessionStorage.getItem(sessionKey);
  if (!sessionId) {
    sessionId = randomId();
    sessionStorage.setItem(sessionKey, sessionId);
  }

  function accessToken() {
    if (!globalThis.CAD_API_AUTH_REQUIRED) return '';
    let token = sessionStorage.getItem(tokenKey) || '';
    if (!token) {
      token = globalThis.prompt(
        'This server is listening beyond localhost. Enter its Canada Policy Studio access token.'
      ) || '';
      if (token) sessionStorage.setItem(tokenKey, token);
    }
    return token;
  }

  const nativeFetch = globalThis.fetch.bind(globalThis);
  globalThis.fetch = (input, init = {}) => {
    const rawUrl = typeof input === 'string' || input instanceof URL ? input : input.url;
    const url = new URL(rawUrl, globalThis.location.href);
    if (url.origin !== globalThis.location.origin || !url.pathname.startsWith('/api/')) {
      return nativeFetch(input, init);
    }

    const headers = new Headers(
      init.headers || (typeof Request !== 'undefined' && input instanceof Request ? input.headers : undefined)
    );
    headers.set('X-CAD-Session-Id', sessionId);
    const token = accessToken();
    if (token) headers.set('Authorization', `Bearer ${token}`);
    return nativeFetch(input, {...init, headers});
  };

  globalThis.CAD_SESSION_ID = sessionId;
  globalThis.CADSetApiToken = token => {
    if (token) sessionStorage.setItem(tokenKey, String(token));
    else sessionStorage.removeItem(tokenKey);
  };
})();
