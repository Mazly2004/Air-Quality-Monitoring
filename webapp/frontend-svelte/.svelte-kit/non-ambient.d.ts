
// this file is generated — do not edit it


declare module "svelte/elements" {
	export interface HTMLAttributes<T> {
		'data-sveltekit-keepfocus'?: true | '' | 'off' | undefined | null;
		'data-sveltekit-noscroll'?: true | '' | 'off' | undefined | null;
		'data-sveltekit-preload-code'?:
			| true
			| ''
			| 'eager'
			| 'viewport'
			| 'hover'
			| 'tap'
			| 'off'
			| undefined
			| null;
		'data-sveltekit-preload-data'?: true | '' | 'hover' | 'tap' | 'off' | undefined | null;
		'data-sveltekit-reload'?: true | '' | 'off' | undefined | null;
		'data-sveltekit-replacestate'?: true | '' | 'off' | undefined | null;
	}
}

export {};


declare module "$app/types" {
	export interface AppTypes {
		RouteId(): "/" | "/analytics" | "/devices" | "/devices/[id]" | "/history" | "/settings";
		RouteParams(): {
			"/devices/[id]": { id: string }
		};
		LayoutParams(): {
			"/": { id?: string };
			"/analytics": Record<string, never>;
			"/devices": { id?: string };
			"/devices/[id]": { id: string };
			"/history": Record<string, never>;
			"/settings": Record<string, never>
		};
		Pathname(): "/" | "/analytics" | "/analytics/" | "/devices" | "/devices/" | `/devices/${string}` & {} | `/devices/${string}/` & {} | "/history" | "/history/" | "/settings" | "/settings/";
		ResolvedPathname(): `${"" | `/${string}`}${ReturnType<AppTypes['Pathname']>}`;
		Asset(): string & {};
	}
}