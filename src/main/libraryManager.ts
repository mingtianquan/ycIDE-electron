/**
 * 支持库管理器（ycmd 版）
 * 扫描 lib 目录中的 *.ycmd.json 清单，并补充窗口单元元数据。
 */
import { app } from 'electron'
import { dirname, join, resolve } from 'path'
import { existsSync, readFileSync, writeFileSync } from 'fs'
import { getYcmdCommands, scanYcmdRegistry, type YcmdResolvedCommand } from './ycmd-registry'

export interface LibraryParam {
  name: string
  type: string
  description: string
  optional: boolean
  isVariable: boolean
  isArray: boolean
  repeatable?: boolean
}

export interface LibraryCommand {
  name: string
  englishName: string
  description: string
  returnType: string
  category: string
  params: LibraryParam[]
  isHidden: boolean
  isMember: boolean
  ownerTypeName: string
  commandIndex: number
  libraryName: string
  libraryFileName: string
  source: 'ycmd' | 'core'
  manifestPath: string
}

export interface LibraryDataType {
  name: string
  englishName: string
  description: string
  isWindowUnit: boolean
}

export interface LibraryConstant {
  name: string
  englishName: string
  description: string
  type: 'null' | 'number' | 'bool' | 'text'
  value: string
}

export interface LibraryWindowUnitProperty {
  name: string
  englishName: string
  description: string
  type: number
  typeName: string
  isReadOnly: boolean
  pickOptions: string[]
}

export interface LibraryWindowUnitEventArg {
  name: string
  description: string
  dataType: string
  isByRef: boolean
}

export interface LibraryWindowUnitEvent {
  name: string
  description: string
  args: LibraryWindowUnitEventArg[]
}

export interface LibraryWindowUnit {
  name: string
  englishName: string
  description: string
  className: string
  style: string
  properties: LibraryWindowUnitProperty[]
  events: LibraryWindowUnitEvent[]
  libraryName: string
}

export interface LibraryInfo {
  name: string
  guid: string
  version: string
  description: string
  author: string
  zipCode: string
  address: string
  phone: string
  qq: string
  email: string
  homePage: string
  otherInfo: string
  fileName: string
  commands: LibraryCommand[]
  dataTypes: LibraryDataType[]
  constants: LibraryConstant[]
  windowUnits: LibraryWindowUnit[]
}

export interface LibraryItem {
  name: string
  filePath: string
  loaded: boolean
  isCore: boolean
  libName?: string
  version?: string
  cmdCount?: number
  dtCount?: number
}

export interface LoadResult {
  success: boolean
  info: LibraryInfo | null
  error?: string
}

interface LibraryMetadataFile {
  description?: string
  author?: string
  homePage?: string
  dataTypes?: unknown
  constants?: unknown
  windowUnits?: unknown
}

interface ParsedLibraryMetadata {
  description: string
  author: string
  homePage: string
  dataTypes: LibraryDataType[]
  constants: LibraryConstant[]
  windowUnits: LibraryWindowUnit[]
}

interface SupportLibraryDescriptor {
  library?: string
  libraryDisplayName?: string
  artifacts?: {
    windows?: Record<string, { staticLib?: string; dynamicLib?: string }>
    linux?: Record<string, { staticLib?: string; dynamicLib?: string }>
    macos?: Record<string, { staticLib?: string; dynamicLib?: string }>
  }
}

const CORE_LIBRARY_NAME = '系统核心支持库'

class LibraryManager {
  private static readonly CORE_LIBRARY_FILE_NAME = 'krnln'
  private libraries: LibraryItem[] = []
  private metadataCache = new Map<string, ParsedLibraryMetadata | null>()
  private supportDescriptorCache = new Map<string, SupportLibraryDescriptor | null>()

  private getConfigPath(): string {
    return join(app.getPath('userData'), 'library-state.json')
  }

  private getSavedLoadedNames(): string[] | null {
    try {
      const cfgPath = this.getConfigPath()
      if (!existsSync(cfgPath)) return null
      const data = JSON.parse(readFileSync(cfgPath, 'utf-8')) as { loadedLibs?: unknown }
      if (!Array.isArray(data.loadedLibs)) return []
      return data.loadedLibs.filter((x): x is string => typeof x === 'string')
    } catch {
      return []
    }
  }

  private saveLoadedState(): void {
    try {
      const loadedLibs = this.libraries.filter(l => l.loaded).map(l => l.name)
      writeFileSync(this.getConfigPath(), JSON.stringify({ loadedLibs }, null, 2), 'utf-8')
    } catch {
      // ignore
    }
  }

  private getLibraryDisplayMeta(customFolder?: string): Map<string, { libName: string; version: string; cmdCount: number }> {
    const root = customFolder || this.getLibFolder()
    const scan = scanYcmdRegistry(root)
    const map = new Map<string, { libName: string; version: string; cmdCount: number }>()

    for (const lib of scan.libraries) {
      let libName = lib.name
      let version = '-'
      let cmdCount = 0

      for (const item of lib.manifests) {
        if (!item.valid || !item.manifest) continue
        cmdCount++
        const manifest = item.manifest as {
          library?: string
          libraryDisplayName?: string
          libraryVersion?: string
          contractVersion?: string
        }

        if (manifest.libraryDisplayName && manifest.libraryDisplayName.trim()) {
          libName = manifest.libraryDisplayName.trim()
        } else if (manifest.library && manifest.library.trim() && libName === lib.name) {
          libName = manifest.library.trim()
        }

        if (manifest.libraryVersion && manifest.libraryVersion.trim()) {
          version = manifest.libraryVersion.trim()
        } else if (version === '-' && manifest.contractVersion && manifest.contractVersion.trim()) {
          version = manifest.contractVersion.trim()
        }
      }

      map.set(lib.name, { libName, version, cmdCount })
    }

    return map
  }

  private getLibraryFolder(name: string): string {
    const scanned = this.libraries.find(lib => lib.name === name)
    return scanned?.filePath || join(this.getLibFolder(), name)
  }

  private getMetadataFileCandidates(name: string, folderPath: string): string[] {
    return [
      join(folderPath, 'window-units.json'),
      join(folderPath, `${name}.window-units.json`),
      join(folderPath, `${name}.metadata.json`),
      join(folderPath, `${name}.library.json`),
    ]
  }

  private parseLibraryDataTypes(value: unknown): LibraryDataType[] {
    if (!Array.isArray(value)) return []
    return value
      .filter((item): item is Record<string, unknown> => !!item && typeof item === 'object')
      .map(item => ({
        name: typeof item.name === 'string' ? item.name.trim() : '',
        englishName: typeof item.englishName === 'string' ? item.englishName.trim() : '',
        description: typeof item.description === 'string' ? item.description.trim() : '',
        isWindowUnit: item.isWindowUnit === true,
      }))
      .filter(item => item.name.length > 0)
  }

  private parseLibraryConstants(value: unknown): LibraryConstant[] {
    if (!Array.isArray(value)) return []
    return value
      .filter((item): item is Record<string, unknown> => !!item && typeof item === 'object')
      .map(item => ({
        name: typeof item.name === 'string' ? item.name.trim() : '',
        englishName: typeof item.englishName === 'string' ? item.englishName.trim() : '',
        description: typeof item.description === 'string' ? item.description.trim() : '',
        type: (item.type === 'number' || item.type === 'bool' || item.type === 'text' ? item.type : 'null') as 'number' | 'bool' | 'text' | 'null',
        value: typeof item.value === 'string' ? item.value : String(item.value ?? ''),
      }))
      .filter(item => item.name.length > 0)
  }

  private parseWindowUnitProperties(value: unknown): LibraryWindowUnitProperty[] {
    if (!Array.isArray(value)) return []
    return value
      .filter((item): item is Record<string, unknown> => !!item && typeof item === 'object')
      .map(item => ({
        name: typeof item.name === 'string' ? item.name.trim() : '',
        englishName: typeof item.englishName === 'string' ? item.englishName.trim() : '',
        description: typeof item.description === 'string' ? item.description.trim() : '',
        type: typeof item.type === 'number' ? item.type : 0,
        typeName: typeof item.typeName === 'string' ? item.typeName.trim() : '文本型',
        isReadOnly: item.isReadOnly === true,
        pickOptions: Array.isArray(item.pickOptions)
          ? item.pickOptions.filter((entry): entry is string => typeof entry === 'string')
          : [],
      }))
      .filter(item => item.name.length > 0)
  }

  private parseWindowUnitEvents(value: unknown): LibraryWindowUnitEvent[] {
    if (!Array.isArray(value)) return []
    return value
      .filter((item): item is Record<string, unknown> => !!item && typeof item === 'object')
      .map(item => ({
        name: typeof item.name === 'string' ? item.name.trim() : '',
        description: typeof item.description === 'string' ? item.description.trim() : '',
        args: Array.isArray(item.args)
          ? item.args
              .filter((arg): arg is Record<string, unknown> => !!arg && typeof arg === 'object')
              .map(arg => ({
                name: typeof arg.name === 'string' ? arg.name.trim() : '',
                description: typeof arg.description === 'string' ? arg.description.trim() : '',
                dataType: typeof arg.dataType === 'string' ? arg.dataType.trim() : '整数型',
                isByRef: arg.isByRef === true,
              }))
              .filter(arg => arg.name.length > 0)
          : [],
      }))
      .filter(item => item.name.length > 0)
  }

  private parseWindowUnits(value: unknown, libraryName: string): LibraryWindowUnit[] {
    if (!Array.isArray(value)) return []
    return value
      .filter((item): item is Record<string, unknown> => !!item && typeof item === 'object')
      .map(item => ({
        name: typeof item.name === 'string' ? item.name.trim() : '',
        englishName: typeof item.englishName === 'string' ? item.englishName.trim() : '',
        description: typeof item.description === 'string' ? item.description.trim() : '',
        className: typeof item.className === 'string' ? item.className.trim() : '',
        style: typeof item.style === 'string' ? item.style.trim() : '',
        properties: this.parseWindowUnitProperties(item.properties),
        events: this.parseWindowUnitEvents(item.events),
        libraryName,
      }))
      .filter(item => item.name.length > 0)
  }

  private getLibraryMetadata(name: string): ParsedLibraryMetadata | null {
    if (this.metadataCache.has(name)) {
      return this.metadataCache.get(name) ?? null
    }

    const folderPath = this.getLibraryFolder(name)
    for (const candidate of this.getMetadataFileCandidates(name, folderPath)) {
      if (!existsSync(candidate)) continue
      try {
        const raw = JSON.parse(readFileSync(candidate, 'utf-8')) as LibraryMetadataFile
        const parsed: ParsedLibraryMetadata = {
          description: typeof raw.description === 'string' ? raw.description.trim() : '',
          author: typeof raw.author === 'string' ? raw.author.trim() : '',
          homePage: typeof raw.homePage === 'string' ? raw.homePage.trim() : '',
          dataTypes: this.parseLibraryDataTypes(raw.dataTypes),
          constants: this.parseLibraryConstants(raw.constants),
          windowUnits: this.parseWindowUnits(raw.windowUnits, name),
        }
        this.metadataCache.set(name, parsed)
        return parsed
      } catch {
        this.metadataCache.set(name, null)
        return null
      }
    }

    this.metadataCache.set(name, null)
    return null
  }

  isCore(name: string): boolean {
    return name === LibraryManager.CORE_LIBRARY_FILE_NAME
  }

  getLibFolder(): string {
    const isDev = !app.isPackaged
    if (isDev) {
      return join(app.getAppPath(), 'lib')
    }
    return join(dirname(process.execPath), 'lib')
  }

  scan(customFolder?: string): LibraryItem[] {
    const root = customFolder || this.getLibFolder()
    const result = scanYcmdRegistry(root)
    const metaMap = this.getLibraryDisplayMeta(root)
    const previousLoaded = new Map(this.libraries.map(l => [l.name, l.loaded]))
    const savedLoaded = this.getSavedLoadedNames()
    const savedSet = savedLoaded ? new Set(savedLoaded) : null

    this.metadataCache.clear()
    this.supportDescriptorCache.clear()

    this.libraries = result.libraries.map(lib => ({
      name: lib.name,
      filePath: lib.folderPath,
      loaded: this.isCore(lib.name)
        ? true
        : (savedSet
            ? savedSet.has(lib.name)
            : (previousLoaded.get(lib.name) ?? true)),
      isCore: this.isCore(lib.name),
      libName: metaMap.get(lib.name)?.libName || lib.name,
      version: metaMap.get(lib.name)?.version || '-',
      cmdCount: metaMap.get(lib.name)?.cmdCount ?? lib.manifests.filter(item => item.valid).length,
      dtCount: this.getLibraryMetadata(lib.name)?.dataTypes.length ?? 0,
    }))

    return this.libraries
  }

  scanAndAutoLoad(): void {
    this.scan()
  }

  load(name: string): LoadResult {
    if (this.libraries.length === 0) this.scan()
    const item = this.libraries.find(l => l.name === name)
    if (!item) return { success: false, info: null, error: `未找到支持库 ${name}` }
    if (!item.loaded) {
      item.loaded = true
      this.saveLoadedState()
    }
    const info = this.getLibInfo(name)
    if (!info) return { success: false, info: null, error: `未找到支持库 ${name}` }
    return { success: true, info }
  }

  unload(name: string): { success: boolean; error?: string } {
    if (this.isCore(name)) {
      return { success: false, error: '核心支持库不可卸载' }
    }
    if (this.libraries.length === 0) this.scan()
    const item = this.libraries.find(l => l.name === name)
    if (!item) return { success: false, error: `未找到支持库 ${name}` }
    if (!item.loaded) return { success: true }
    item.loaded = false
    this.saveLoadedState()
    return { success: true }
  }

  applySelection(selectedNames: string[]): { loadedCount: number; unloadedCount: number; failed: Array<{ name: string; error: string }> } {
    if (this.libraries.length === 0) this.scan()

    const failed: Array<{ name: string; error: string }> = []
    const selected = new Set(selectedNames)
    selected.add(LibraryManager.CORE_LIBRARY_FILE_NAME)

    let loadedCount = 0
    let unloadedCount = 0

    for (const item of this.libraries) {
      const targetLoaded = this.isCore(item.name) ? true : selected.has(item.name)
      if (item.loaded === targetLoaded) continue

      if (!targetLoaded && this.isCore(item.name)) {
        failed.push({ name: item.name, error: '核心支持库不可卸载' })
        continue
      }

      if (targetLoaded) {
        item.loaded = true
        loadedCount++
      } else {
        item.loaded = false
        unloadedCount++
      }
    }

    this.saveLoadedState()
    return { loadedCount, unloadedCount, failed }
  }

  loadAll(): number {
    if (this.libraries.length === 0) this.scan()
    let changed = 0
    for (const item of this.libraries) {
      if (!item.loaded) {
        item.loaded = true
        changed++
      }
    }
    if (changed > 0) this.saveLoadedState()
    return changed
  }

  getList(): LibraryItem[] {
    return this.scan()
  }

  private mapYcmdCommand(cmd: YcmdResolvedCommand): LibraryCommand {
    return {
      ...cmd,
      params: (cmd.params || []).map(p => ({
        name: p.name,
        type: p.type,
        description: p.description,
        optional: !!p.optional,
        isVariable: !!p.isVariable,
        isArray: !!p.isArray,
        repeatable: !!p.repeatable,
      })),
    }
  }

  getAllCommands(): LibraryCommand[] {
    if (this.libraries.length === 0) this.scan()
    const loadedSet = new Set(this.libraries.filter(l => l.loaded).map(l => l.name))
    const commands: LibraryCommand[] = [
      ...getYcmdCommands()
      .filter(cmd => loadedSet.has(cmd.libraryFileName))
      .map(cmd => this.mapYcmdCommand(cmd)),
    ]

    const deduped = new Map<string, LibraryCommand>()
    for (const command of commands) {
      const owner = (command.ownerTypeName || '').trim()
      const dedupeKey = command.isMember ? `${command.name}@@member@@${owner}` : `${command.name}@@global`
      if (!deduped.has(dedupeKey)) deduped.set(dedupeKey, command)
    }
    return Array.from(deduped.values())
  }

  getAllDataTypes(): LibraryDataType[] {
    if (this.libraries.length === 0) this.scan()
    return this.libraries
      .filter(lib => lib.loaded)
      .flatMap(lib => this.getLibraryMetadata(lib.name)?.dataTypes || [])
  }

  getLibInfo(name: string): LibraryInfo | null {
    const isCoreLibrary = name === LibraryManager.CORE_LIBRARY_FILE_NAME || name === CORE_LIBRARY_NAME
    const commands = [
      ...getYcmdCommands()
      .map(cmd => this.mapYcmdCommand(cmd))
      .filter(cmd => cmd.libraryFileName === name || cmd.libraryName === name),
    ]
    const metadata = this.getLibraryMetadata(name)

    if (commands.length === 0 && !metadata) return null

    const displayMeta = this.getLibraryDisplayMeta().get(name)
    return {
      name: isCoreLibrary ? CORE_LIBRARY_NAME : (displayMeta?.libName || name),
      guid: '-',
      version: displayMeta?.version || '-',
      description: metadata?.description || (isCoreLibrary ? '系统核心支持库内建命令与元数据。' : '由 ycmd 清单生成'),
      author: metadata?.author || '-',
      zipCode: '-',
      address: '-',
      phone: '-',
      qq: '-',
      email: '-',
      homePage: metadata?.homePage || '-',
      otherInfo: '-',
      fileName: name,
      commands,
      dataTypes: metadata?.dataTypes || [],
      constants: metadata?.constants || [],
      windowUnits: metadata?.windowUnits || [],
    }
  }

  getAllWindowUnits(): LibraryWindowUnit[] {
    if (this.libraries.length === 0) this.scan()
    return this.libraries
      .filter(lib => lib.loaded)
      .flatMap(lib => this.getLibraryMetadata(lib.name)?.windowUnits || [])
  }

  findStaticLib(_name: string, _arch: string): string | null {
    if (this.libraries.length === 0) this.scan()
    const folder = this.getLibraryFolder(_name)
    const descriptor = this.getSupportDescriptor(_name, folder)
    if (!descriptor?.artifacts) return null

    const platformKey = this.getHostPlatformKey()
    const archKey = this.normalizeArchKey(_arch)
    const bucket = descriptor.artifacts[platformKey]
    if (!bucket) return null
    const item = bucket[archKey] || bucket.x64 || bucket.x86
    if (!item?.staticLib) return null
    const abs = resolve(folder, item.staticLib)
    return existsSync(abs) ? abs : null
  }

  getLoadedLibraryFiles(): Array<{ name: string; libraryPath: string; libName: string }> {
    if (this.libraries.length === 0) this.scan()
    const platformKey = this.getHostPlatformKey()
    const archKey = this.normalizeArchKey(process.arch)

    return this.libraries
      .filter(lib => lib.loaded)
      .map(lib => {
        const folder = this.getLibraryFolder(lib.name)
        const descriptor = this.getSupportDescriptor(lib.name, folder)
        const artifacts = descriptor?.artifacts?.[platformKey]
        const item = artifacts ? (artifacts[archKey] || artifacts.x64 || artifacts.x86) : undefined
        let libraryPath = ''
        if (item?.dynamicLib) {
          const candidate = resolve(folder, item.dynamicLib)
          if (existsSync(candidate)) libraryPath = candidate
        }
        if (!libraryPath && item?.staticLib) {
          const candidate = resolve(folder, item.staticLib)
          if (existsSync(candidate)) libraryPath = candidate
        }

        return {
          name: lib.name,
          libraryPath,
          libName: lib.libName || lib.name,
        }
      })
  }

  private getHostPlatformKey(): 'windows' | 'linux' | 'macos' {
    if (process.platform === 'win32') return 'windows'
    if (process.platform === 'darwin') return 'macos'
    return 'linux'
  }

  private normalizeArchKey(arch: string): string {
    const t = (arch || '').toLowerCase()
    if (t === 'x64' || t === 'amd64') return 'x64'
    if (t === 'x86' || t === 'ia32' || t === 'win32') return 'x86'
    if (t === 'arm64' || t === 'aarch64') return 'arm64'
    return t || 'x64'
  }

  private getSupportDescriptor(name: string, folderPath: string): SupportLibraryDescriptor | null {
    if (this.supportDescriptorCache.has(name)) {
      return this.supportDescriptorCache.get(name) ?? null
    }

    const candidates = [
      join(folderPath, `${name}.ysup.json`),
      join(folderPath, 'library.ysup.json'),
    ]

    for (const file of candidates) {
      if (!existsSync(file)) continue
      try {
        const parsed = JSON.parse(readFileSync(file, 'utf-8')) as SupportLibraryDescriptor
        this.supportDescriptorCache.set(name, parsed)
        return parsed
      } catch {
        this.supportDescriptorCache.set(name, null)
        return null
      }
    }

    this.supportDescriptorCache.set(name, null)
    return null
  }
}

export const libraryManager = new LibraryManager()
