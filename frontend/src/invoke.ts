import { Call } from '@wailsio/runtime'

const invokeMethodCandidates = [
  'main.ProcessService.Invoke',
  'changeme.ProcessService.Invoke',
]

async function invokeCppFunction(functionName: string, params: unknown[]): Promise<unknown> {
  let lastError: unknown

  for (const methodName of invokeMethodCandidates) {
    try {
      return await Call.ByName(methodName, functionName, params)
    } catch (error: unknown) {
      const errorName =
        typeof error === 'object' && error !== null && 'name' in error
          ? String((error as { name: unknown }).name)
          : ''

      if (errorName === 'ReferenceError') {
        lastError = error
        continue
      }

      throw error
    }
  }

  throw lastError ?? new Error('未找到 ProcessService.Invoke 绑定方法')
}

declare global {
  interface String {
    invoke(...params: unknown[]): Promise<unknown>
  }
}

Object.defineProperty(String.prototype, 'invoke', {
  value: function invoke(this: string, ...params: unknown[]) {
    return invokeCppFunction(this.toString(), params)
  },
  writable: true,
  configurable: true,
})

export {}
