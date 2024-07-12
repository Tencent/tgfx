import { image } from "@kit.ImageKit";

export const draw: (index: number) => void;

export const updateDensity: (density: number) => void;

export const addImageFromPixelMap: (name: string, image: image.PixelMap) => void;

export const addImageFromPath: (name: string, imagePath: string) => void;

export const addImageFromEncoded: (name: string, data: ArrayBuffer) => void;