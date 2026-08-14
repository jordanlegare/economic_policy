(() => {
  'use strict';

  const sessionKey = 'cad-policy-studio.session-id';
  const tokenKey = 'cad-policy-studio.api-token';
  const lastRunKey = 'cad-policy-studio.last-run-settings.v1';
  const replaySafeMutationPaths = new Set(['/api/room', '/api/negotiation']);
  const delegationSchema = 'cad-policy-studio/delegation-settings';
  const delegationVersion = 1;
  const delegationScalarFields = Object.freeze({
    usTariff: [0, 200],
    retaliatoryTariff: [0, 60],
    canadaPriority: [0, 100],
    usPriority: [0, 100],
    riskAversion: [0, 100],
    cooperationCeiling: [0, 100]
  });
  const scalarRunFields = delegationScalarFields;

  function randomId() {
    if (globalThis.crypto?.randomUUID) return globalThis.crypto.randomUUID();
    if (globalThis.crypto?.getRandomValues) {
      const bytes = new Uint8Array(16);
      globalThis.crypto.getRandomValues(bytes);
      return Array.from(bytes, b => b.toString(16).padStart(2, '0')).join('');
    }
    return `session-${Date.now()}-${Math.random().toString(36).slice(2)}`;
  }

  function storageGet(storage, key) {
    try { return storage?.getItem?.(key) || ''; } catch (_) { return ''; }
  }

  function storageSet(storage, key, value) {
    try { storage?.setItem?.(key, value); return true; } catch (_) { return false; }
  }

  function storageRemove(storage, key) {
    try { storage?.removeItem?.(key); } catch (_) {}
  }

  function clampNumber(value, minimum, maximum) {
    const number = Number(value);
    if (!Number.isFinite(number)) return null;
    return Math.max(minimum, Math.min(maximum, number));
  }

  function strictNumber(value, minimum, maximum, label) {
    const number = Number(value);
    if (!Number.isFinite(number)) throw new Error(`${label} must be numeric`);
    if (number < minimum || number > maximum)
      throw new Error(`${label} must be between ${minimum} and ${maximum}`);
    return number;
  }

  function normalizeDelegationSettings(payload) {
    if (!payload || typeof payload !== 'object' || Array.isArray(payload))
      throw new Error('delegation settings must be an object');
    const settings = {};
    for (const [field, bounds] of Object.entries(delegationScalarFields)) {
      if (!Object.prototype.hasOwnProperty.call(payload, field))
        throw new Error(`${field} is required`);
      settings[field] = strictNumber(payload[field], bounds[0], bounds[1], field);
    }
    if (Math.abs(settings.canadaPriority + settings.usPriority - 100) > 1e-6)
      throw new Error('Canada and U.S. outcome weights must sum to 100');
    for (const party of ['us', 'canada']) {
      const key = `${party}SectorCoverage`;
      const values = payload[key];
      if (!Array.isArray(values) || values.length !== 20)
        throw new Error(`${key} must contain exactly 20 sector values`);
      settings[key] = values.map((value, index) =>
        strictNumber(value, 0, 100, `${key}[${index}]`));
    }
    return settings;
  }

  function cleanMetadata(value, fallback = '') {
    if (typeof value !== 'string') return fallback;
    return value.trim().replace(/[\u0000-\u001f\u007f]/g, '').slice(0, 120) || fallback;
  }

  function buildDelegationPackage(settings, metadata = {}) {
    const normalized = normalizeDelegationSettings(settings);
    const exportedAt = typeof metadata.exportedAt === 'string'
      && !Number.isNaN(Date.parse(metadata.exportedAt))
      ? new Date(metadata.exportedAt).toISOString()
      : new Date().toISOString();
    return {
      schema: delegationSchema,
      version: delegationVersion,
      application: 'Canada Tariff Observatory',
      exportedAt,
      exportedBy: cleanMetadata(metadata.exportedBy, 'Joint dashboard'),
      settings: normalized
    };
  }

  function validateDelegationPackage(payload) {
    if (!payload || typeof payload !== 'object' || Array.isArray(payload))
      throw new Error('delegation package must be a JSON object');
    if (payload.schema !== delegationSchema)
      throw new Error(`unsupported delegation package schema; expected ${delegationSchema}`);
    if (Number(payload.version) !== delegationVersion)
      throw new Error(`unsupported delegation package version; expected ${delegationVersion}`);
    return {
      schema: delegationSchema,
      version: delegationVersion,
      application: cleanMetadata(payload.application, 'Foreign delegation package'),
      exportedAt: typeof payload.exportedAt === 'string' && !Number.isNaN(Date.parse(payload.exportedAt))
        ? new Date(payload.exportedAt).toISOString() : '',
      exportedBy: cleanMetadata(payload.exportedBy, 'Foreign delegation'),
      settings: normalizeDelegationSettings(payload.settings)
    };
  }

  function negotiationExchangePayload(settings) {
    const normalized = normalizeDelegationSettings(settings);
    const payload = {
      actor: 'exchange',
      usTariff: normalized.usTariff,
      retaliatoryTariff: normalized.retaliatoryTariff,
      canadaPriority: normalized.canadaPriority,
      usPriority: normalized.usPriority,
      riskAversion: normalized.riskAversion,
      cooperationCeiling: normalized.cooperationCeiling
    };
    ['us', 'canada'].forEach(party => {
      normalized[`${party}SectorCoverage`].forEach((value, index) => {
        payload[`${party}Sector${index}`] = value;
      });
    });
    return payload;
  }

  function sanitizeRunSettings(payload, preserveSavedAt = false) {
    if (!payload || typeof payload !== 'object') return null;
    const savedAt = preserveSavedAt && typeof payload.savedAt === 'string'
      && !Number.isNaN(Date.parse(payload.savedAt))
      ? payload.savedAt
      : new Date().toISOString();
    const settings = {version: 1, savedAt};
    let populated = false;

    Object.entries(scalarRunFields).forEach(([field, bounds]) => {
      const value = clampNumber(payload[field], bounds[0], bounds[1]);
      if (value !== null) {
        settings[field] = value;
        populated = true;
      }
    });

    ['us', 'canada'].forEach(party => {
      const storedCoverage = payload[`${party}SectorCoverage`];
      const coverage = [];
      for (let i = 0; i < 20; i++) {
        const rawValue = Array.isArray(storedCoverage)
          ? storedCoverage[i]
          : payload[`${party}Sector${i}`];
        const value = clampNumber(rawValue, 0, 100);
        if (value === null) return;
        coverage.push(value);
      }
      if (coverage.length === 20) {
        settings[`${party}SectorCoverage`] = coverage;
        populated = true;
      }
    });

    return populated ? settings : null;
  }

  function readLastRunSettings() {
    try {
      const parsed = JSON.parse(storageGet(globalThis.localStorage, lastRunKey) || 'null');
      return parsed && parsed.version === 1
        ? sanitizeRunSettings(parsed, true)
        : null;
    } catch (_) {
      return null;
    }
  }

  function writeLastRunSettings(payload) {
    const settings = sanitizeRunSettings(payload);
    if (!settings) return false;
    return storageSet(globalThis.localStorage, lastRunKey, JSON.stringify(settings));
  }

  function captureEvaluateRequest(url, init) {
    if (url.pathname !== '/api/evaluate'
        || String(init.method || 'GET').toUpperCase() !== 'POST'
        || typeof init.body !== 'string') return;
    try {
      const payload = JSON.parse(init.body);
      // Comparators, diagnostics and robustness probes are derived work, not an
      // operator run. Only the primary stateful evaluation becomes the restart
      // checkpoint, regardless of when secondary requests are scheduled.
      if (payload?.comparisonOnly === true) return;
      writeLastRunSettings(payload);
    } catch (_) {}
  }

  function setControlValue(id, value) {
    const control = globalThis.document?.getElementById?.(id);
    if (control && value !== undefined) control.value = String(value);
    return control;
  }

  function restoreLastRunSettings(saved = readLastRunSettings()) {
    if (!saved || !globalThis.document) return false;

    const opening = globalThis.InitialOpeningScenario?.opening;
    if (opening) {
      if (saved.usTariff !== undefined) opening.usTariff = saved.usTariff;
      if (saved.retaliatoryTariff !== undefined) opening.retaliatoryTariff = saved.retaliatoryTariff;
      if (saved.usSectorCoverage?.length === 20) {
        opening.usSectorCoverage.splice(0, opening.usSectorCoverage.length, ...saved.usSectorCoverage);
      }
      if (saved.canadaSectorCoverage?.length === 20) {
        opening.canadaSectorCoverage.splice(0, opening.canadaSectorCoverage.length, ...saved.canadaSectorCoverage);
      }
    }

    setControlValue('usTariff', saved.usTariff);
    setControlValue('retaliatoryTariff', saved.retaliatoryTariff);
    setControlValue('canadaPriority', saved.canadaPriority);
    setControlValue('usPriority', saved.usPriority);
    setControlValue('riskAversion', saved.riskAversion);
    setControlValue('cooperationCeiling', saved.cooperationCeiling);

    if (typeof positions !== 'undefined') {
      if (saved.usSectorCoverage?.length === 20) {
        positions.us.splice(0, positions.us.length, ...saved.usSectorCoverage);
      }
      if (saved.canadaSectorCoverage?.length === 20) {
        positions.canada.splice(0, positions.canada.length, ...saved.canadaSectorCoverage);
      }
    }

    const canadaPriorityValue = globalThis.document.getElementById('canadaPriorityValue');
    if (canadaPriorityValue && saved.canadaPriority !== undefined) {
      canadaPriorityValue.textContent = `${saved.canadaPriority}%`;
    }
    const usPriorityValue = globalThis.document.getElementById('usPriorityValue');
    if (usPriorityValue && saved.usPriority !== undefined)
      usPriorityValue.textContent = `${saved.usPriority}%`;
    const priorityTotal = globalThis.document.getElementById('priorityTotal');
    if (priorityTotal && saved.canadaPriority !== undefined && saved.usPriority !== undefined) {
      priorityTotal.textContent = `${saved.canadaPriority + saved.usPriority}%`;
    }
    const riskValue = globalThis.document.getElementById('riskAversionValue');
    if (riskValue && saved.riskAversion !== undefined)
      riskValue.textContent = String(saved.riskAversion);
    const cooperationValue = globalThis.document.getElementById('cooperationCeilingValue');
    if (cooperationValue && saved.cooperationCeiling !== undefined) {
      cooperationValue.textContent = `${saved.cooperationCeiling}%`;
    }
    const retaliationValue = globalThis.document.getElementById('retaliatoryTariffValue');
    if (retaliationValue && saved.retaliatoryTariff !== undefined) {
      retaliationValue.textContent = `${saved.retaliatoryTariff}%`;
    }

    if (typeof updateTariff === 'function') updateTariff();
    if (typeof updatePosition === 'function') updatePosition();
    if (typeof syncPartyView === 'function') syncPartyView();
    if (typeof refreshPartySectorMetrics === 'function') refreshPartySectorMetrics();
    const partyView = globalThis.document.getElementById('partyView');
    if (partyView && !partyView.hidden && typeof renderPartySectors === 'function')
      renderPartySectors();
    return true;
  }

  function armInitialRestore() {
    const saved = readLastRunSettings();
    if (!saved) return;

    restoreLastRunSettings(saved);
    const currentEvaluate = globalThis.evaluate;
    if (typeof currentEvaluate !== 'function' || currentEvaluate.__cadRestoresLastRun) return;

    let pending = true;
    const wrappedEvaluate = async function(...args) {
      if (pending) {
        pending = false;
        restoreLastRunSettings(saved);
      }
      return currentEvaluate.apply(this, args);
    };
    wrappedEvaluate.__cadRestoresLastRun = true;
    globalThis.evaluate = wrappedEvaluate;
  }

  function currentDelegationLabel() {
    const view = globalThis.document?.querySelector?.('.header-tabs button.active')?.dataset?.view;
    if (view === 'canada') return 'Canada delegation';
    if (view === 'us') return 'U.S. delegation';
    return 'Joint dashboard';
  }

  function captureCurrentDelegationSettings() {
    const saved = readLastRunSettings() || {};
    const controlValue = (id, fallback) => {
      const raw = globalThis.document?.getElementById?.(id)?.value;
      return raw === undefined || raw === '' ? fallback : raw;
    };
    let usCoverage = saved.usSectorCoverage;
    let canadaCoverage = saved.canadaSectorCoverage;
    try {
      if (typeof positions !== 'undefined') {
        usCoverage = Array.from(positions.us || []);
        canadaCoverage = Array.from(positions.canada || []);
      }
    } catch (_) {}
    return normalizeDelegationSettings({
      usTariff: controlValue('usTariff', saved.usTariff),
      retaliatoryTariff: controlValue('retaliatoryTariff', saved.retaliatoryTariff),
      canadaPriority: controlValue('canadaPriority', saved.canadaPriority),
      usPriority: controlValue('usPriority', saved.usPriority),
      riskAversion: controlValue('riskAversion', saved.riskAversion),
      cooperationCeiling: controlValue('cooperationCeiling', saved.cooperationCeiling),
      usSectorCoverage: usCoverage,
      canadaSectorCoverage: canadaCoverage
    });
  }

  function setDelegationExchangeStatus(message, state = 'ready', detail = '') {
    const node = globalThis.document?.getElementById?.('delegationExchangeStatus');
    if (!node) return;
    node.textContent = message;
    node.dataset.state = state;
    node.title = detail || message;
  }

  function exportDelegationSettings() {
    const settings = captureCurrentDelegationSettings();
    const packageData = buildDelegationPackage(settings, {exportedBy: currentDelegationLabel()});
    const BlobCtor = globalThis.Blob;
    if (!BlobCtor || typeof globalThis.URL?.createObjectURL !== 'function')
      throw new Error('this browser cannot create an export file');
    const blob = new BlobCtor([`${JSON.stringify(packageData, null, 2)}\n`], {
      type: 'application/json;charset=utf-8'
    });
    const href = globalThis.URL.createObjectURL(blob);
    const anchor = globalThis.document.createElement('a');
    anchor.href = href;
    anchor.download = `delegation-settings-${packageData.exportedAt.replace(/[:.]/g, '-')}.json`;
    anchor.hidden = true;
    globalThis.document.body.appendChild(anchor);
    anchor.click();
    anchor.remove();
    globalThis.setTimeout?.(() => globalThis.URL.revokeObjectURL(href), 0);
    setDelegationExchangeStatus('Exported', 'success', `${packageData.exportedBy} settings package exported`);
    return packageData;
  }

  async function importDelegationSettingsPackage(payload) {
    const validated = validateDelegationPackage(payload);
    setDelegationExchangeStatus('Importing…', 'busy', `Valid package from ${validated.exportedBy}`);
    const response = await globalThis.fetch('/api/negotiation', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify(negotiationExchangePayload(validated.settings))
    });
    let state = {};
    try { state = await response.json(); } catch (_) {}
    if (!response.ok) throw new Error(state?.error || `server rejected import (${response.status})`);

    writeLastRunSettings(validated.settings);
    try {
      if (typeof applyNegotiation === 'function') applyNegotiation(state, false);
      else restoreLastRunSettings({...validated.settings, version: 1, savedAt: new Date().toISOString()});
    } catch (_) {
      restoreLastRunSettings({...validated.settings, version: 1, savedAt: new Date().toISOString()});
    }
    setDelegationExchangeStatus('Imported', 'success', `Imported from ${validated.exportedBy}`);

    try {
      if (typeof evaluate === 'function') await evaluate();
      else globalThis.document?.getElementById?.('run')?.click?.();
    } catch (error) {
      setDelegationExchangeStatus('Imported · rerun needed', 'warning', String(error?.message || error));
    }
    return {package: validated, state};
  }

  async function importDelegationSettingsFile(file) {
    if (!file) return null;
    if (Number(file.size || 0) > 262144) throw new Error('delegation package is larger than 256 KB');
    const text = await file.text();
    let payload;
    try { payload = JSON.parse(text); }
    catch (_) { throw new Error('delegation package is not valid JSON'); }
    return importDelegationSettingsPackage(payload);
  }

  function installDelegationExchange() {
    const document = globalThis.document;
    const host = document?.querySelector?.('header .status') || document?.querySelector?.('.status');
    if (!host || host.dataset?.delegationExchangeInstalled === 'true') return false;

    if (!document.getElementById('delegationExchangeStyles') && document.head?.appendChild) {
      const style = document.createElement('style');
      style.id = 'delegationExchangeStyles';
      style.textContent = `
        .status.delegation-exchange{display:flex;align-items:center;justify-content:flex-end;gap:6px;min-width:0;font-size:9px}
        .delegation-exchange-label{display:inline-flex;align-items:center;gap:6px;color:#65736e;white-space:nowrap}
        .delegation-exchange-label:before{content:'⇄';display:grid;place-items:center;width:20px;height:20px;border:1px solid #d9d6ce;border-radius:50%;color:#27745e;background:#fff}
        .delegation-exchange-actions{display:flex;gap:5px}
        .delegation-exchange button{border:1px solid #d9d6ce;border-radius:999px;background:#fff;color:#33443e;padding:7px 10px;font:600 9px/1 'DM Sans',sans-serif;white-space:nowrap;box-shadow:0 2px 8px rgba(25,35,41,.04)}
        .delegation-exchange button:hover,.delegation-exchange button:focus-visible{border-color:#27745e;color:#1f6752;outline:none;box-shadow:0 4px 14px rgba(39,116,94,.12)}
        #delegationExchangeStatus[data-state="success"]{color:#27745e}#delegationExchangeStatus[data-state="warning"]{color:#9b6416}#delegationExchangeStatus[data-state="error"]{color:#b52b3a}#delegationExchangeStatus[data-state="busy"]{color:#2d668c}
        @media(max-width:980px){.delegation-exchange-label{display:none}}
        @media(max-width:720px){header{gap:8px}.status.delegation-exchange{display:flex}.delegation-exchange button{padding:7px 8px;font-size:8px}}
      `;
      document.head.appendChild(style);
    }

    host.dataset.delegationExchangeInstalled = 'true';
    host.classList?.add?.('delegation-exchange');
    host.innerHTML = `<span class="delegation-exchange-label" id="delegationExchangeStatus" data-state="ready">Delegation exchange</span><span class="delegation-exchange-actions"><button id="exportDelegationSettings" type="button" title="Export all current bilateral delegation settings to JSON">Export settings</button><button id="importDelegationSettings" type="button" title="Import a foreign delegation settings package">Import foreign</button></span><input id="delegationSettingsFile" type="file" accept="application/json,.json" hidden><span id="sync" hidden aria-hidden="true"></span>`;

    const exportButton = document.getElementById('exportDelegationSettings');
    const importButton = document.getElementById('importDelegationSettings');
    const fileInput = document.getElementById('delegationSettingsFile');
    exportButton?.addEventListener?.('click', () => {
      try { exportDelegationSettings(); }
      catch (error) {
        setDelegationExchangeStatus('Export failed', 'error', String(error?.message || error));
        globalThis.alert?.(`Could not export delegation settings: ${error?.message || error}`);
      }
    });
    importButton?.addEventListener?.('click', () => fileInput?.click?.());
    fileInput?.addEventListener?.('change', async () => {
      const file = fileInput.files?.[0];
      try {
        await importDelegationSettingsFile(file);
      } catch (error) {
        setDelegationExchangeStatus('Import rejected', 'error', String(error?.message || error));
        globalThis.alert?.(`Could not import delegation settings: ${error?.message || error}`);
      } finally {
        fileInput.value = '';
      }
    });
    return true;
  }

  // Session identity is not secret and should survive a browser restart so the
  // server-side room/event history can be reopened. Keep the bearer token in
  // sessionStorage; only the non-sensitive session id is durable.
  let sessionId = storageGet(globalThis.localStorage, sessionKey)
    || storageGet(globalThis.sessionStorage, sessionKey);
  if (!sessionId) sessionId = randomId();
  storageSet(globalThis.localStorage, sessionKey, sessionId);
  storageSet(globalThis.sessionStorage, sessionKey, sessionId);

  let mutationSequence = 0;
  function nextMutationOperationId() {
    mutationSequence += 1;
    const entropy = randomId().replace(/[^A-Za-z0-9._:-]/g, '').slice(0, 16);
    return `mut:${sessionId}:${Date.now().toString(36)}:${mutationSequence.toString(36)}:${entropy}`;
  }

  function prepareReplaySafeMutation(url, init) {
    if (!replaySafeMutationPaths.has(url.pathname)
        || String(init.method || 'GET').toUpperCase() !== 'POST'
        || typeof init.body !== 'string') return {init, retryable:false};
    try {
      const payload = JSON.parse(init.body);
      if (!payload || typeof payload !== 'object' || Array.isArray(payload))
        return {init, retryable:false};
      if (typeof payload.operationId !== 'string' || !payload.operationId)
        payload.operationId = nextMutationOperationId();
      return {
        init:{...init, body:JSON.stringify(payload)},
        retryable:true
      };
    } catch (_) {
      return {init, retryable:false};
    }
  }

  function accessToken() {
    if (!globalThis.CAD_API_AUTH_REQUIRED) return '';
    let token = storageGet(globalThis.sessionStorage, tokenKey);
    if (!token) {
      token = globalThis.prompt(
        'This server is listening beyond localhost. Enter its Canada Policy Studio access token.'
      ) || '';
      if (token) storageSet(globalThis.sessionStorage, tokenKey, token);
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

    const prepared = prepareReplaySafeMutation(url, init);
    const requestInit = prepared.init;
    captureEvaluateRequest(url, requestInit);
    const headers = new Headers(
      requestInit.headers
      || (typeof Request !== 'undefined' && input instanceof Request ? input.headers : undefined)
    );
    headers.set('X-CAD-Session-Id', sessionId);
    const token = accessToken();
    if (token) headers.set('Authorization', `Bearer ${token}`);
    const finalInit = {...requestInit, headers};
    const send = () => nativeFetch(input, finalInit);
    if (!prepared.retryable) return send();

    // One replay-safe retry covers a dropped response or transient admission
    // failure. The identical operationId/body is reused, so a first request that
    // committed before the connection failed cannot mutate state twice.
    return send().then(response => {
      if (response && [502, 503, 504].includes(Number(response.status))) return send();
      return response;
    }, () => send());
  };

  function startBrowserSession() {
    armInitialRestore();
    installDelegationExchange();
  }
  if (globalThis.document?.readyState === 'loading') {
    globalThis.document.addEventListener('DOMContentLoaded', startBrowserSession, {once: true});
  } else {
    startBrowserSession();
  }

  globalThis.CAD_SESSION_ID = sessionId;
  globalThis.CADSetApiToken = token => {
    if (token) storageSet(globalThis.sessionStorage, tokenKey, String(token));
    else storageRemove(globalThis.sessionStorage, tokenKey);
  };
  globalThis.CADLastRunSettings = {
    key: lastRunKey,
    read: readLastRunSettings,
    write: writeLastRunSettings,
    restore: restoreLastRunSettings
  };
  globalThis.CADDelegationExchange = {
    schema: delegationSchema,
    version: delegationVersion,
    normalizeSettings: normalizeDelegationSettings,
    buildPackage: buildDelegationPackage,
    validatePackage: validateDelegationPackage,
    negotiationPayload: negotiationExchangePayload,
    capture: captureCurrentDelegationSettings,
    exportPackage: exportDelegationSettings,
    importPackage: importDelegationSettingsPackage,
    install: installDelegationExchange
  };
})();
