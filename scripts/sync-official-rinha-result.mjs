#!/usr/bin/env node
import { execFileSync } from 'node:child_process';
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = dirname(dirname(fileURLToPath(import.meta.url)));
const REPO = 'zanfranceschi/rinha-de-backend-2026';
const AUTHOR = 'jonathanperis';
const TARGET_REPO_URL = 'https://github.com/jonathanperis/rinha4-back-end-c';
const OUT_DIR = join(ROOT, 'docs/public/official');
const ISSUE_SEARCH = 'reason:completed rinha/test';

function gh(args) {
  return execFileSync('gh', args, { cwd: ROOT, encoding: 'utf8', env: process.env, stdio: ['ignore', 'pipe', 'pipe'] });
}

function extractJsonBlocks(markdown) {
  const blocks = [];
  const fenced = /```(?:json)?\s*([\s\S]*?)```/gi;
  let match;
  while ((match = fenced.exec(markdown)) !== null) blocks.push(match[1].trim());
  return blocks;
}

function parseResultComment(body) {
  for (const block of extractJsonBlocks(body ?? '')) {
    try {
      const parsed = JSON.parse(block);
      if (parsed?.['test-results']?.scoring) return parsed;
    } catch {}
  }
  return null;
}

function main() {
  const issues = JSON.parse(gh(['issue', 'list', '--repo', REPO, '--author', AUTHOR, '--state', 'closed', '--search', ISSUE_SEARCH, '--json', 'number,title,url,updatedAt,closedAt', '--limit', '50']));
  const runs = [];
  for (const issue of issues) {
    const viewed = JSON.parse(gh(['issue', 'view', String(issue.number), '--repo', REPO, '--comments', '--json', 'number,title,url,updatedAt,closedAt,comments']));
    for (const comment of viewed.comments ?? []) {
      const parsed = parseResultComment(comment.body);
      if (!parsed) continue;
      const repoUrl = parsed['repo-url'] ?? '';
      if (repoUrl !== TARGET_REPO_URL) continue;
      runs.push({
        issue: { number: viewed.number, title: viewed.title, url: viewed.url, updated_at: viewed.updatedAt, closed_at: viewed.closedAt },
        comment: { url: comment.url, created_at: comment.createdAt, author: comment.author?.login ?? '' },
        repo_url: repoUrl,
        result: parsed['test-results'],
        runtime_info: parsed['runtime-info'] ?? null,
      });
    }
  }
  runs.sort((a, b) => Date.parse(b.comment.created_at || b.issue.closed_at || '') - Date.parse(a.comment.created_at || a.issue.closed_at || ''));
  if (runs.length === 0) throw new Error(`No official result comments found for ${AUTHOR} and ${TARGET_REPO_URL}`);
  const synced = new Date().toISOString();
  const latest = { synced_at: synced, source: { repo: REPO, author: AUTHOR, search: ISSUE_SEARCH, target_repo_url: TARGET_REPO_URL }, ...runs[0] };
  mkdirSync(OUT_DIR, { recursive: true });
  writeFileSync(join(OUT_DIR, 'latest.json'), `${JSON.stringify(latest, null, 2)}\n`);
  writeFileSync(join(OUT_DIR, 'index.json'), `${JSON.stringify({ synced_at: synced, runs }, null, 2)}\n`);
  console.log(`Synced official result #${latest.issue.number}: p99=${latest.result.p99}`);
}

main();
