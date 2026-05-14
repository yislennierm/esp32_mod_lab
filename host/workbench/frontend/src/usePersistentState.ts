import { useEffect, useState } from 'react';

function loadStoredValue<T>(key: string, initialValue: T) {
  if (typeof window === 'undefined') return initialValue;
  try {
    const raw = window.localStorage.getItem(key);
    if (!raw) return initialValue;
    return JSON.parse(raw) as T;
  } catch {
    return initialValue;
  }
}

export function usePersistentState<T>(key: string, initialValue: T) {
  const [value, setValue] = useState<T>(() => loadStoredValue(key, initialValue));

  useEffect(() => {
    if (typeof window === 'undefined') return;
    try {
      window.localStorage.setItem(key, JSON.stringify(value));
    } catch {
      return;
    }
  }, [key, value]);

  return [value, setValue] as const;
}
