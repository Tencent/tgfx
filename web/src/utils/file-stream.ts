export class FileStream {
    filePath: string = "";
    blob: Blob | null = null;
    reader: FileReader | null = null;

    public constructor(filePath: string) {
        this.filePath = filePath;
    }

    private async fetchFile(): Promise<Blob> {
        if (!this.blob) {
            const response = await fetch(this.filePath);
            this.blob = await response.blob();
        }
        return this.blob;
    }

    private createFileReader(): FileReader {
        if (!this.reader) {
            this.reader = new FileReader();
        }
        return this.reader;
    }

    public async read(start: number, length: number): Promise<Uint8Array> {
        try {
            const blob = await this.fetchFile();
            const reader = this.createFileReader();

            return new Promise<Uint8Array>((resolve, reject) => {
                reader.onload = (event) => {
                    const buffer = event.target?.result as ArrayBuffer;
                    const data = new Uint8Array(buffer);
                    resolve(data);
                };

                reader.onerror = (event) => {
                    reject(new Error(`Error: ${event.target?.error}`));
                };

                const end = Math.min(start + length, blob.size);
                reader.readAsArrayBuffer(blob.slice(start, end));
            });
        } catch (error) {
            throw new Error(`Fetch error: ${error}`);
        }
    }

    public async length(): Promise<number> {
        const blob = await this.fetchFile();
        if (blob) {
            return blob.size;
        }
        return 0;
    }
}