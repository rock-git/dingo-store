/*
 * Copyright 2021 DataCanvas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package io.dingodb.client;

import io.dingodb.client.operation.Operation;
import io.dingodb.sdk.common.DingoClientException;
import io.dingodb.sdk.common.DingoCommonId;
import io.dingodb.sdk.common.codec.DingoKeyValueCodec;
import io.dingodb.sdk.common.codec.KeyValueCodec;
import io.dingodb.sdk.common.concurrent.Executors;
import io.dingodb.sdk.common.partition.RangeStrategy;
import io.dingodb.sdk.common.table.RangeDistribution;
import io.dingodb.sdk.common.table.Table;
import io.dingodb.sdk.common.utils.Any;
import io.dingodb.sdk.common.utils.ByteArrayUtils;
import io.dingodb.sdk.common.utils.Parameters;
import io.dingodb.sdk.service.connector.MetaServiceConnector;
import io.dingodb.sdk.service.meta.MetaServiceClient;
import io.dingodb.sdk.service.store.StoreServiceClient;
import lombok.extern.slf4j.Slf4j;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.NavigableMap;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

@Slf4j
public class OperationService {

    private static Map<DingoCommonId, RouteTable> dingoRouteTables = new ConcurrentHashMap<>();

    private final MetaServiceConnector metaServiceConnector;
    private final MetaServiceClient rootMetaService;
    private final StoreServiceClient storeService;

    public OperationService(String coordinatorSvr, int retryTimes) {
        this.rootMetaService = new MetaServiceClient(coordinatorSvr);
        this.metaServiceConnector = (MetaServiceConnector) rootMetaService.getMetaConnector();
        this.storeService = new StoreServiceClient(rootMetaService, retryTimes);
    }

    public void init() {
    }

    public void close() {
        storeService.shutdown();
        metaServiceConnector.shutdown();
        rootMetaService.getIncrementConnector().shutdown();
    }

    public <R> R exec(String schemaName, String tableName, Operation operation, Object parameters) {
        MetaServiceClient metaService = Parameters
            .nonNull(rootMetaService.getSubMetaService(schemaName), "Schema not found: " + schemaName);
        DingoCommonId tableId = Parameters.nonNull(metaService.getTableId(tableName), "Table not found: " + tableName);
        Table table = metaService.getTableDefinition(tableId);
        RouteTable routeTable = getAndRefreshRouteTable(metaService, tableId, false);

        Operation.Fork fork = operation.fork(Any.wrap(parameters), table, routeTable);
        List<OperationContext> contexts = generateContext(tableId, table, routeTable.getCodec(), fork);
        AtomicReference<Throwable> error = new AtomicReference<>();
        if (contexts.size() == 1) {
            operation.exec(contexts.get(0));
        } else {
            CountDownLatch countDownLatch = new CountDownLatch(contexts.size());
            contexts.forEach(context -> CompletableFuture
                .runAsync(() -> operation.exec(context), Executors.executor("exec-operator"))
                .whenComplete((r, e) -> {
                         countDownLatch.countDown();
                         error.set(e);
                 }));
            try {
                countDownLatch.await();
            } catch (InterruptedException e) {
                throw new RuntimeException(e);
            }
        }
        if (!fork.isIgnoreError() && error.get() != null) {
            throw new DingoClientException(-1, error.get());
        }
        return operation.reduce(fork);
    }

    private List<OperationContext> generateContext(
        DingoCommonId tableId, Table table, KeyValueCodec codec, Operation.Fork fork
    ) {
        int i = 0;
        List<OperationContext> contexts = new ArrayList<>(fork.getSubTasks().size());
        for (Operation.Task subTask : fork.getSubTasks()) {
            contexts.add(new OperationContext(
                tableId, subTask.getRegionId(), table, codec, storeService, i++, subTask.getParameters(), Any.wrap(fork.result())
            ));
        }
        return contexts;
    }

    public synchronized boolean createTable(String schema, String name, Table table) {
        MetaServiceClient metaService = Parameters
            .nonNull(rootMetaService.getSubMetaService(schema), "Schema not found: " + schema);
        return metaService.createTable(name, table);
    }

    public boolean dropTable(String schema, String tableName) {
        MetaServiceClient metaService = Parameters
            .nonNull(rootMetaService.getSubMetaService(schema), "Schema not found: " + schema);
        return metaService.dropTable(tableName);
    }

    public Table getTableDefinition(String schema, String tableName) {
        MetaServiceClient metaService = Parameters
            .nonNull(rootMetaService.getSubMetaService(schema), "Schema not found: " + schema);
        return metaService.getTableDefinition(tableName);
    }

    public synchronized RouteTable getAndRefreshRouteTable(
        MetaServiceClient metaService, DingoCommonId tableId, boolean isRefresh
    ) {
        if (isRefresh) {
            dingoRouteTables.remove(tableId);
        }
        RouteTable routeTable = dingoRouteTables.get(tableId);
        if (routeTable == null) {
            Table table = metaService.getTableDefinition(tableId);
            if (table == null) {
                return null;
            }

            NavigableMap<ByteArrayUtils.ComparableByteArray, RangeDistribution> parts =
                    metaService.getRangeDistribution(table.getName());

            KeyValueCodec keyValueCodec = new DingoKeyValueCodec(
                    table.getDingoType(),
                    table.getKeyMapping(),
                    tableId.entityId()
            );

            RangeStrategy rangeStrategy = new RangeStrategy(parts.navigableKeySet(), keyValueCodec);

            routeTable = new RouteTable(tableId, keyValueCodec, parts, rangeStrategy);

            dingoRouteTables.put(tableId, routeTable);
        }

        return routeTable;
    }

}