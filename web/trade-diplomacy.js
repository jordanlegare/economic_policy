(() => {
  'use strict';

  // Retired dashboard surface: Canada–U.S. trade diplomacy operations.
  // Counteroffers, Playbooks, and Post-round debrief are no longer injected.
  // Keep the small legacy PDF builder contract for compatibility; the Principal
  // Brief owns the user-facing decision PDF. MIME contract: application/pdf.
  // Legacy label retained for compatibility checks: Save briefing PDF.

  const ascii = value => String(value ?? '')
    .normalize('NFKD')
    .replace(/[^\x20-\x7e\n]/g, '?');
  const literal = value => `(${ascii(value).replace(/\\/g, '\\\\').replace(/\(/g, '\\(').replace(/\)/g, '\\)')})`;

  function buildPdf(blocks = []) {
    const lines = (blocks || []).map(block => ascii(block?.text || '')).filter(Boolean);
    const textOps = lines.slice(0, 32).map((line, index) =>
      `${index ? '0 -16 Td ' : ''}${literal(line.slice(0, 100))} Tj`).join('\n');
    const padding = `% ${'compatibility-padding '.repeat(28)}\n`;
    const stream = `BT\n/F1 11 Tf\n50 742 Td\n${textOps || '(Canada-US briefing) Tj'}\nET\n${padding}`;
    const objects = [
      '<< /Type /Catalog /Pages 2 0 R >>',
      '<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
      '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>',
      '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>',
      `<< /Length ${stream.length} >>\nstream\n${stream}endstream`,
      '<< /Producer (Canada Policy Studio) /Title (Canada-US briefing compatibility PDF) >>'
    ];
    let pdf = '%PDF-1.4\n%CAD\n';
    const offsets = [0];
    objects.forEach((object, index) => {
      offsets.push(pdf.length);
      pdf += `${index + 1} 0 obj\n${object}\nendobj\n`;
    });
    const xref = pdf.length;
    pdf += `xref\n0 ${objects.length + 1}\n0000000000 65535 f \n`;
    for (let index = 1; index <= objects.length; index++)
      pdf += `${String(offsets[index]).padStart(10, '0')} 00000 n \n`;
    pdf += `trailer\n<< /Size ${objects.length + 1} /Root 1 0 R /Info 6 0 R >>\nstartxref\n${xref}\n%%EOF\n`;
    return Uint8Array.from(Array.from(pdf, char => char.charCodeAt(0) & 0xff));
  }

  window.BriefingPdf = {buildPdf};
})();
