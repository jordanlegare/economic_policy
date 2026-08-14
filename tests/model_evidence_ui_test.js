'use strict';

const fs = require('fs');

const source = fs.readFileSync('web/calibration.js', 'utf8');

if (source.includes("section.id = 'calibrationTrust'") || source.includes('id="calibrationTrust"'))
  throw new Error('Data provenance & calibration dashboard surface must remain retired');
if (source.includes("section.id = 'modelEvidence'") || source.includes('id="modelEvidence"'))
  throw new Error('Model evidence V2 dashboard surface must remain retired');
if (source.includes('document.createElement'))
  throw new Error('retired calibration/evidence module must not inject dashboard DOM');

for (const endpoint of ['/api/v2/backtests', '/api/v2/welfare', '/api/v2/robustness']) {
  if (!source.includes(endpoint))
    throw new Error(`retired UI module must document preserved backend endpoint ${endpoint}`);
}

console.log('retired model evidence dashboard contract tests passed');
