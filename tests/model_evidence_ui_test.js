'use strict';

const fs = require('fs');

const source = fs.readFileSync('web/calibration.js', 'utf8');

const required = [
  '#modelEvidence',
  'Model evidence V2',
  '#calibrationStructuralSummary',
  'Structural production calibration',
  'Direct empirical mappings',
  'Shock variances',
  'Remaining multipliers',
  'Still provisional',
  'direct empirical mappings',
  'Observed-data calibration completeness',
  'structuralCalibration',
  'directEmpiricalCoverage',
  'directEmpiricalShockCount',
  'directEmpiricalMultiplierCount',
  'provisionalCount',
  '/api/v2/structural-registry',
  '/api/v2/backtests',
  '/api/v2/welfare',
  '/api/v2/robustness',
  'runHistoricalEvidence',
  'runWelfareEvidence',
  'runStructuralEvidence',
  'interactive 6-draw run',
  'not a probability distribution or confidence interval',
  'do not constitute statistical validation'
];

for (const token of required) {
  if (!source.includes(token)) throw new Error(`missing V2 evidence UI contract token: ${token}`);
}

if (/addEventListener\(['"]DOMContentLoaded['"],\s*async/.test(source)) {
  throw new Error('heavy V2 diagnostics must not run automatically at DOMContentLoaded');
}

const historicalHandler = source.indexOf("#runHistoricalEvidence");
const welfareHandler = source.indexOf("#runWelfareEvidence");
const structuralHandler = source.indexOf("#runStructuralEvidence");
if (!(historicalHandler >= 0 && welfareHandler >= 0 && structuralHandler >= 0)) {
  throw new Error('V2 evidence diagnostics must remain explicit user actions');
}

const provenancePanel = source.indexOf('id="calibrationStructuralSummary"');
const integrityGates = source.indexOf('Integrity gates');
if (!(provenancePanel >= 0 && integrityGates > provenancePanel)) {
  throw new Error('structural calibration completeness must appear in the Data provenance & calibration panel before integrity gates');
}

console.log('V2 model evidence UI contract tests passed');
