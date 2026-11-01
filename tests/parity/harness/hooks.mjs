// Node module hooks for loading the TypeScript reference unmodified:
// - resolve extensionless relative imports to `.ts`
// - expose the (unexported) FACTORY preset list from src/ui/presets.ts
// - skip Vite-only `?worker&url` imports
import { existsSync } from 'node:fs'
import { fileURLToPath } from 'node:url'

export async function resolve(specifier, context, next) {
  if (specifier.includes('?worker')) return { url: 'data:text/javascript,export default ""', shortCircuit: true }
  if ((specifier.startsWith('.') || specifier.startsWith('/')) && !/\.[cm]?[jt]s$/.test(specifier) && context.parentURL) {
    const url = new URL(specifier + '.ts', context.parentURL)
    if (existsSync(fileURLToPath(url))) return next(url.href, context)
  }
  return next(specifier, context)
}

export async function load(url, context, next) {
  const result = await next(url, context)
  if (url.endsWith('/src/ui/presets.ts')) {
    const src = String(result.source) + '\nexport { FACTORY }\n'
    return { ...result, source: src }
  }
  return result
}
