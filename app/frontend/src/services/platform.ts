import type { MilahProjectPayload } from "../core/types";

export interface OpenedFile {
  name: string;
  path: string;
  role: "manuscript" | "translation";
  content: string;
}

export interface PlatformGateway {
  openOsisFiles(role: "manuscript" | "translation"): Promise<OpenedFile[]>;
  openProject(): Promise<(MilahProjectPayload & { projectPath?: string }) | null>;
  saveProject(payload: MilahProjectPayload): Promise<boolean>;
  exportCombinedOsis(osis: string): Promise<boolean>;
  lastError(): Promise<string>;
}

interface NativeObject {
  openOsisFiles(role: string, callback: (files: OpenedFile[]) => void): void;
  openProject(callback: (payload: MilahProjectPayload) => void): void;
  saveProject(payload: MilahProjectPayload, callback: (saved: boolean) => void): void;
  exportCombinedOsis(osis: string, callback: (saved: boolean) => void): void;
  lastError: string;
}

declare global {
  interface Window {
    qt?: { webChannelTransport: unknown };
    QWebChannel?: new (
      transport: unknown,
      callback: (channel: { objects: { milahNative: NativeObject } }) => void,
    ) => unknown;
  }
}

class QtGateway implements PlatformGateway {
  constructor(private readonly native: NativeObject) {}

  openOsisFiles(role: "manuscript" | "translation"): Promise<OpenedFile[]> {
    return new Promise((resolve) => this.native.openOsisFiles(role, resolve));
  }

  openProject(): Promise<(MilahProjectPayload & { projectPath?: string }) | null> {
    return new Promise((resolve) =>
      this.native.openProject((payload) =>
        resolve(payload && Object.keys(payload).length ? payload : null),
      ),
    );
  }

  saveProject(payload: MilahProjectPayload): Promise<boolean> {
    return new Promise((resolve) => this.native.saveProject(payload, resolve));
  }

  exportCombinedOsis(osis: string): Promise<boolean> {
    return new Promise((resolve) =>
      this.native.exportCombinedOsis(osis, resolve),
    );
  }

  async lastError(): Promise<string> {
    return this.native.lastError ?? "";
  }
}

class BrowserGateway implements PlatformGateway {
  async openOsisFiles(
    role: "manuscript" | "translation",
  ): Promise<OpenedFile[]> {
    const input = document.createElement("input");
    input.type = "file";
    input.accept = ".osis,.xml,text/xml,application/xml";
    input.multiple = true;
    const files = await new Promise<File[]>((resolve) => {
      input.addEventListener("change", () => resolve([...(input.files ?? [])]));
      input.click();
    });
    return Promise.all(
      files.map(async (file) => ({
        name: file.name,
        path: file.name,
        role,
        content: await file.text(),
      })),
    );
  }

  async openProject(): Promise<null> {
    throw new Error("Browser project bundles require the future web ZIP adapter.");
  }

  async saveProject(): Promise<boolean> {
    throw new Error("Browser project bundles require the future web ZIP adapter.");
  }

  async exportCombinedOsis(osis: string): Promise<boolean> {
    const blob = new Blob([osis], { type: "application/xml;charset=utf-8" });
    const link = document.createElement("a");
    link.href = URL.createObjectURL(blob);
    link.download = "Milah_Combined.osis";
    link.click();
    URL.revokeObjectURL(link.href);
    return true;
  }

  async lastError(): Promise<string> {
    return "";
  }
}

export async function connectPlatform(): Promise<PlatformGateway> {
  const transport = window.qt?.webChannelTransport;
  const WebChannel = window.QWebChannel;
  if (transport && WebChannel) {
    return new Promise((resolve) => {
      new WebChannel(transport, (channel) => {
        resolve(new QtGateway(channel.objects.milahNative));
      });
    });
  }
  return new BrowserGateway();
}
