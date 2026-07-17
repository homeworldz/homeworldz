/// <reference types="vite/client" />

declare module "vitre-js" {
  export const Vitre: {
    apply(root?: ParentNode, features?: string[]): void;
  };
}
