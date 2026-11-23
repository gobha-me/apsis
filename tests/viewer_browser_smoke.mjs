// Optional real Chromium smoke. Install Playwright/browser in owned scratch,
// serve tools/viewer over HTTP, then run: node tests/viewer_browser_smoke.mjs URL
// If run.json is absent, the test intercepts that request with the pinned recording.
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { pathToFileURL } from 'node:url';

export async function viewerBrowserSmoke(page, origin) {
  const failures = [];
  page.on('pageerror', (error) => failures.push(error.message));
  await page.goto(origin);
  await page.getByText('Playback is paused at the baseline.', { exact: false }).waitFor();
  assert.equal(await page.locator('#entity').inputValue(), 'pilot');
  assert.equal(await page.locator('#inspection tr').count(), 12);
  assert.match(await page.locator('#inspection').innerText(), /pilot\.wallet/);
  await page.getByRole('button', { name: 'Step one day', exact: true }).click();
  assert.match(await page.locator('#day').innerText(), /1\/720/);
  await page.locator('#entity').selectOption('war');
  assert.equal(await page.locator('#inspection tr').count(), 3);
  await page.locator('#position').evaluate((slider) => { slider.value = '720'; });
  await page.locator('#position').dispatchEvent('input');
  assert.match(await page.locator('#day').innerText(), /Day 9720/);
  assert.equal(await page.getByRole('button', { name: 'Step one day', exact: true }).isDisabled(), true);
  await page.getByRole('button', { name: 'Play', exact: true }).click();
  await page.getByRole('button', { name: 'Pause', exact: true }).click();
  await page.getByRole('button', { name: 'Reload run', exact: true }).click();
  await page.getByText('Playback is paused at the baseline.', { exact: false }).waitFor();
  assert.match(await page.locator('#day').innerText(), /Day 9000/);
  assert.equal(await page.locator('#entity').inputValue(), 'pilot');
  assert.deepEqual(failures, []);
  return { checks: 'load, inspect, step, scrub, play, pause, reload', pageErrors: failures };
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const { chromium } = await import('playwright');
  const artifact = await readFile(new URL('fixtures/viewer/recorded-run.json', import.meta.url), 'utf8');
  const browser = await chromium.launch({ headless: true });
  try {
    const page = await browser.newPage();
    await page.route('**/run.json', (route) => route.fulfill({ contentType: 'application/json', body: artifact }));
    console.log(await viewerBrowserSmoke(page, process.argv[2] ?? 'http://127.0.0.1:8000/'));
  } finally {
    await browser.close();
  }
}
