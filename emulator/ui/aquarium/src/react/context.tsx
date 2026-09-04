// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

import { createContext, useContext, ReactNode, useMemo } from 'react';
import { AquariumClient } from '../core/client';
import { AuthProvider } from '../core/auth';

const AquariumContext = createContext<AquariumClient | null>(null);

export interface AquariumProviderProps {
  /**
   * Existing pre-configured AquariumClient instance.
   */
  client?: AquariumClient;

  /**
   * gRPC-Web proxy / server URI endpoint (e.g. "http://localhost:8085").
   * Used if client is not explicitly provided.
   */
  uri?: string;

  /**
   * Static JWT authentication token string.
   */
  token?: string;

  /**
   * Static or dynamic JWT auth provider.
   */
  auth?: AuthProvider;

  children: ReactNode;
}

/**
 * Top-level React Context Provider for Aquarium.
 */
export function AquariumProvider({ client, uri, token, auth, children }: AquariumProviderProps) {
  const instance = useMemo(() => {
    if (client) return client;
    if (uri) {
      return AquariumClient.create({
        endpoint: uri,
        authProvider: auth || token,
      });
    }
    throw new Error('AquariumProvider requires either a "client" or a "uri" prop.');
  }, [client, uri, token, auth]);

  return (
    <AquariumContext.Provider value={instance}>
      {children}
    </AquariumContext.Provider>
  );
}

/**
 * Hook to access the root AquariumClient instance from context.
 */
export function useAquariumClient(): AquariumClient {
  const client = useContext(AquariumContext);
  if (!client) {
    throw new Error('useAquariumClient must be used within an <AquariumProvider>');
  }
  return client;
}
