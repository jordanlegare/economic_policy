# Parameterized search concurrency invariant

Repeated evaluations must not accumulate background no-tariff comparison searches.

The browser should keep at most one comparison request in flight and retain only the newest queued comparison. A completed comparison remains reusable when a newer primary evaluation made its UI sequence stale, provided the comparison key is unchanged.

The server should single-flight identical `comparisonOnly` request bodies and cache the most recent completed comparison response. Primary stateful evaluations remain ordered per session.

This invariant prevents repeated Run actions from turning one expensive comparison into an increasing number of concurrent exhaustive searches competing for the fixed worker pool.
