import { Invoke } from '../bindings/changeme/processservice'


declare global {
  interface String {
    invoke(...params: unknown[]): Promise<unknown>
  }
}

Object.defineProperty(String.prototype, 'invoke', {
  value: async function invoke(this: string, ...params: unknown[]) {
    await Invoke(this.toString(), params)
  },
  writable: true,
  configurable: true,
})

export { }
