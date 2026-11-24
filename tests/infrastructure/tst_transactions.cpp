// SPDX-FileCopyrightText: 2026 Elis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "application/result.h"
#include "application/transaction.h"
#include "application/use_cases.h"
#include "domain/asset.h"
#include "domain/jurisdiction.h"
#include "domain/money.h"
#include "domain/phase.h"
#include "domain/profile.h"
#include "domain/year_month.h"
#include "infrastructure/sqlite/database.h"
#include "infrastructure/sqlite/repositories.h"
#include "infrastructure/sqlite/sqlite_transaction.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

using namespace hinkwise;
using namespace hinkwise::application;
using namespace hinkwise::infrastructure;

namespace {

constexpr domain::YearMonth kMonth{2026, 3};

domain::Money sek(qint64 minorUnits)
{
    return domain::Money{minorUnits, domain::Currency::Sek};
}

domain::Profile sampleProfile()
{
    domain::Profile profile;
    profile.displayName = "Elis";
    profile.baseCurrency = domain::Currency::Sek;
    profile.phase = domain::Phase::BuildingWealth;
    profile.jurisdiction = domain::swedishJurisdiction();
    return profile;
}

} // namespace

/// The transaction boundary is infrastructure behaviour, so it is tested against
/// a real SQLite file rather than a double: what has to be true is that a row
/// written inside a boundary is on disk after a commit and gone after anything
/// else, and only the file can say that.
///
/// The failures are arranged the way the storage fails: a deferred foreign key
/// that only reports at COMMIT, and a trigger that refuses one particular write.
/// Nothing a household can type makes the repositories fail halfway, which is
/// exactly why the boundary has to be tested from below rather than from the
/// screens.
class TestTransactions : public QObject {
    Q_OBJECT

private slots:
    void init();

    void commitStoresEverything();
    void droppingWithoutCommitRollsBack();
    void aFailedCommitRollsBack();
    void aSecondBeginIsRefusedAndLeavesTheOuterAlone();
    void changePhaseStoresTheHistoryAndTheProfile();
    void changePhaseRollsBackAHistoryEntryTheProfileNeverGot();

private:
    QSqlDatabase connection() const;

    /// Stores the household these repositories belong to and returns its id,
    /// so every scoped call has an owner to filter on.
    std::int64_t seedProfile();

    /// Runs one statement on the test's own connection, for the two things a
    /// repository cannot do: defer a constraint, and refuse a write.
    bool run(const QString& sql) const;

    /// The number of assets another connection sees, which is what "persisted"
    /// means: reading on the writing connection would show rows a rollback has
    /// already taken away.
    int assetCountOnDisk() const;

    std::optional<QTemporaryDir> m_directory;
    std::optional<sqlite::Database> m_database;
};

void TestTransactions::init()
{
    // A new file per test, so no test can read another one's rows.
    m_database.reset();
    m_directory.emplace();
    QVERIFY(m_directory->isValid());

    const QString path = m_directory->filePath(QStringLiteral("hinkwise.db"));
    auto opened = sqlite::Database::open(path);
    QVERIFY2(opened.ok(), opened.error().c_str());
    m_database.emplace(std::move(opened.value()));

    const Status migrated = m_database->migrate();
    QVERIFY2(migrated.ok(), migrated.error().c_str());
}

QSqlDatabase TestTransactions::connection() const
{
    return m_database->connection();
}

std::int64_t TestTransactions::seedProfile()
{
    sqlite::SqliteProfileRepository profiles{connection()};
    if (!profiles.save(sampleProfile()).ok())
        return 0;

    const Result<std::optional<domain::Profile>> loaded = profiles.load();
    if (!loaded.ok() || !loaded.value())
        return 0;
    return loaded.value()->id;
}

bool TestTransactions::run(const QString& sql) const
{
    QSqlQuery query{connection()};
    return query.exec(sql);
}

int TestTransactions::assetCountOnDisk() const
{
    auto opened = sqlite::Database::open(m_database->path());
    if (!opened.ok())
        return -1;

    sqlite::SqliteProfileRepository profiles{opened.value().connection()};
    const Result<std::optional<domain::Profile>> active = profiles.load();
    if (!active.ok() || !active.value())
        return -1;

    sqlite::SqliteAssetRepository assets{opened.value().connection()};
    assets.setActiveProfile(active.value()->id);
    const Result<std::vector<domain::Asset>> all = assets.all();
    if (!all.ok())
        return -1;
    return static_cast<int>(all.value().size());
}

void TestTransactions::commitStoresEverything()
{
    const std::int64_t profileId = seedProfile();
    QVERIFY(profileId > 0);

    sqlite::SqliteTransactionFactory factory{connection()};
    Result<std::unique_ptr<Transaction>> begun = factory.begin();
    QVERIFY2(begun.ok(), begun.error().c_str());

    sqlite::SqliteAssetRepository assets{connection()};
    assets.setActiveProfile(profileId);
    sqlite::SqliteValuationRepository valuations{connection()};
    valuations.setActiveProfile(profileId);
    QVERIFY2(assets.upsert(domain::makeAsset("fund", "Fund", domain::AssetKind::Fund)).ok(),
             "asset");
    QVERIFY2(valuations.upsert("fund", kMonth, sek(1000000)).ok(), "valuation");

    const Status committed = begun.value()->commit();
    QVERIFY2(committed.ok(), committed.error().c_str());

    // Both writes were part of the commit, so both are in the file.
    QCOMPARE(assetCountOnDisk(), 1);
    const Result<std::optional<domain::Money>> latest = valuations.latest("fund");
    QVERIFY2(latest.ok(), latest.error().c_str());
    QVERIFY(latest.value().has_value());
    QCOMPARE(latest.value()->minorUnits(), qint64{1000000});
}

void TestTransactions::droppingWithoutCommitRollsBack()
{
    const std::int64_t profileId = seedProfile();
    QVERIFY(profileId > 0);

    sqlite::SqliteAssetRepository assets{connection()};
    assets.setActiveProfile(profileId);
    sqlite::SqliteValuationRepository valuations{connection()};
    valuations.setActiveProfile(profileId);

    {
        sqlite::SqliteTransactionFactory factory{connection()};
        Result<std::unique_ptr<Transaction>> begun = factory.begin();
        QVERIFY2(begun.ok(), begun.error().c_str());

        QVERIFY2(assets.upsert(domain::makeAsset("fund", "Fund", domain::AssetKind::Fund)).ok(),
                 "asset");
        QVERIFY2(valuations.upsert("fund", kMonth, sek(1000000)).ok(), "valuation");
        // No commit: the transaction is dropped here, which is the ordinary way
        // an operation gives up.
    }

    QCOMPARE(assetCountOnDisk(), 0);
    const Result<std::optional<domain::Money>> latest = valuations.latest("fund");
    QVERIFY2(latest.ok(), latest.error().c_str());
    QVERIFY(!latest.value().has_value());
}

void TestTransactions::aFailedCommitRollsBack()
{
    const std::int64_t profileId = seedProfile();
    QVERIFY(profileId > 0);

    sqlite::SqliteAssetRepository assets{connection()};
    assets.setActiveProfile(profileId);
    sqlite::SqliteValuationRepository valuations{connection()};
    valuations.setActiveProfile(profileId);

    sqlite::SqliteTransactionFactory factory{connection()};
    Result<std::unique_ptr<Transaction>> begun = factory.begin();
    QVERIFY2(begun.ok(), begun.error().c_str());

    // With the check deferred, a row that breaks the foreign key is written and
    // the COMMIT is what refuses it: the one way to make a real commit fail
    // without breaking the file underneath.
    QVERIFY(run(QStringLiteral("PRAGMA defer_foreign_keys = ON")));

    QVERIFY2(assets.upsert(domain::makeAsset("fund", "Fund", domain::AssetKind::Fund)).ok(),
             "asset");
    // Written below the repository because the repository now refuses a
    // valuation for an asset that does not belong to the active profile. The
    // deferred check is what makes COMMIT refuse this row.
    QVERIFY2(run(QStringLiteral("INSERT INTO valuation (asset_id, month, amount_minor, currency) "
                                "VALUES ('no-such-asset', '2026-03', 500, 'SEK')")),
             "deferred check");

    const Status committed = begun.value()->commit();
    QVERIFY(!committed.ok());
    QVERIFY(!committed.error().empty());

    // A refused commit rolls back, and the asset written before it goes too.
    QCOMPARE(assetCountOnDisk(), 0);
}

void TestTransactions::aSecondBeginIsRefusedAndLeavesTheOuterAlone()
{
    const std::int64_t profileId = seedProfile();
    QVERIFY(profileId > 0);

    sqlite::SqliteTransactionFactory factory{connection()};
    Result<std::unique_ptr<Transaction>> outer = factory.begin();
    QVERIFY2(outer.ok(), outer.error().c_str());

    Result<std::unique_ptr<Transaction>> inner = factory.begin();
    QVERIFY(!inner.ok());
    QVERIFY(!inner.error().empty());

    // The refusal is not a rollback: the outer transaction still owns its work
    // and commits it.
    sqlite::SqliteAssetRepository assets{connection()};
    assets.setActiveProfile(profileId);
    QVERIFY2(assets.upsert(domain::makeAsset("fund", "Fund", domain::AssetKind::Fund)).ok(),
             "asset within the outer transaction");

    const Status committed = outer.value()->commit();
    QVERIFY2(committed.ok(), committed.error().c_str());
    QCOMPARE(assetCountOnDisk(), 1);
}

void TestTransactions::changePhaseStoresTheHistoryAndTheProfile()
{
    const std::int64_t profileId = seedProfile();
    QVERIFY(profileId > 0);

    sqlite::SqliteProfileRepository profiles{connection()};
    sqlite::SqlitePhaseHistoryRepository history{connection()};
    history.setActiveProfile(profileId);
    sqlite::SqliteTransactionFactory factory{connection()};

    domain::Profile profile = sampleProfile();
    const Status changed = changePhase(profiles, history, factory, profile,
                                       domain::Phase::Preserving, "2026-03-15T12:00:00Z");
    QVERIFY2(changed.ok(), changed.error().c_str());
    QCOMPARE(static_cast<int>(profile.phase), static_cast<int>(domain::Phase::Preserving));

    const Result<std::optional<domain::Profile>> stored = profiles.load();
    QVERIFY2(stored.ok(), stored.error().c_str());
    QVERIFY(stored.value().has_value());
    QCOMPARE(static_cast<int>(stored.value()->phase), static_cast<int>(domain::Phase::Preserving));

    const Result<std::vector<PhaseChange>> entries = history.all();
    QVERIFY2(entries.ok(), entries.error().c_str());
    QCOMPARE(entries.value().size(), std::size_t{1});
}

void TestTransactions::changePhaseRollsBackAHistoryEntryTheProfileNeverGot()
{
    const std::int64_t profileId = seedProfile();
    QVERIFY(profileId > 0);

    sqlite::SqliteProfileRepository profiles{connection()};
    sqlite::SqlitePhaseHistoryRepository history{connection()};
    history.setActiveProfile(profileId);
    sqlite::SqliteTransactionFactory factory{connection()};

    // The profile refuses to be written, so the failure lands on the second and
    // last write of the use case, with the history entry already stored.
    QVERIFY2(run(QStringLiteral("CREATE TRIGGER refuse_profile_write BEFORE UPDATE ON profile "
                                "BEGIN SELECT RAISE(ABORT, 'the test refuses this save'); END")),
             "trigger");

    domain::Profile profile = sampleProfile();
    const Status changed = changePhase(profiles, history, factory, profile,
                                       domain::Phase::Preserving, "2026-03-15T12:00:00Z");
    QVERIFY(!changed.ok());
    QVERIFY(!changed.error().empty());

    // A history entry that claimed a change the profile never got would be worse
    // than no change: neither the entry nor the phase survives.
    QCOMPARE(static_cast<int>(profile.phase), static_cast<int>(domain::Phase::BuildingWealth));

    const Result<std::vector<PhaseChange>> entries = history.all();
    QVERIFY2(entries.ok(), entries.error().c_str());
    QVERIFY(entries.value().empty());

    const Result<std::optional<domain::Profile>> stored = profiles.load();
    QVERIFY2(stored.ok(), stored.error().c_str());
    QVERIFY(stored.value().has_value());
    QCOMPARE(static_cast<int>(stored.value()->phase),
             static_cast<int>(domain::Phase::BuildingWealth));
}

QTEST_GUILESS_MAIN(TestTransactions)

#include "tst_transactions.moc"
