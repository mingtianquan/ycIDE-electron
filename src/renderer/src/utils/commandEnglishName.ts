interface CommandNameContext {
  englishName?: string
  libraryName?: string
  category?: string
}

const CORE_LIBRARY_HINT = '核心支持库'

const CATEGORY_PREFIX_MAP: Array<{ match: string; prefix: string }> = [
  { match: '磁盘操作', prefix: 'krnln.disk' },
  { match: '流程控制', prefix: 'krnln.flow' },
  { match: '逻辑比较', prefix: 'krnln.logic' },
  { match: '算术运算', prefix: 'krnln.math' },
  { match: '文本操作', prefix: 'krnln.text' },
  { match: '字节集操作', prefix: 'krnln.bin' },
  { match: '程序调试', prefix: 'krnln.debug' },
]

export function formatCommandEnglishNameForHint(ctx: CommandNameContext): string {
  const englishName = (ctx.englishName || '').trim()
  if (!englishName) return ''
  if (englishName.includes('.')) return englishName

  const lib = (ctx.libraryName || '').trim()
  if (!lib.includes(CORE_LIBRARY_HINT)) return englishName

  const category = (ctx.category || '').trim()
  const normalized = englishName.toLowerCase()
  const found = CATEGORY_PREFIX_MAP.find(x => category.includes(x.match))
  if (found) return `${found.prefix}.${normalized}`
  return `krnln.core.${normalized}`
}

