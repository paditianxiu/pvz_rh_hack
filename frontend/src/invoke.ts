import { Invoke } from "../bindings/changeme/processservice"






declare global {
  interface String {
    invoke(...params: unknown[]): Promise<unknown>
  }
}

Object.defineProperty(String.prototype, 'invoke', {
  value: async function invoke(this: string, ...params: unknown[]) {
    const resutl = await Invoke(this.toString(), params)
    return resutl
  },
  writable: true,
  configurable: true,
})

export { }
