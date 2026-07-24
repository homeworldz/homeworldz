package estate

import (
	"context"
	"testing"
)

func TestMemoryStoreForRegionCreatesAndSharesDefault(t *testing.T) {
	ctx := context.Background()
	store := NewMemoryStore()
	owner := "11111111-1111-4111-8111-111111111111"

	first, err := store.ForRegion(ctx, "region-a", owner)
	if err != nil {
		t.Fatal(err)
	}
	if first.ID != DefaultEstateID || first.Name != DefaultEstateName || first.OwnerUserID != owner {
		t.Fatalf("unexpected default estate: %#v", first)
	}
	if !first.PublicAccess || !first.UseGlobalTime {
		t.Fatalf("default estate should be public with global time: %#v", first)
	}
	if first.ParentEstateID != MainlandEstateID {
		t.Fatalf("default parent estate = %d", first.ParentEstateID)
	}

	// A second region owned by the same owner shares the same estate.
	second, err := store.ForRegion(ctx, "region-b", owner)
	if err != nil {
		t.Fatal(err)
	}
	if second.ID != first.ID {
		t.Fatalf("co-owned regions should share an estate: %d vs %d", second.ID, first.ID)
	}

	// A different owner gets a distinct estate id.
	other, err := store.ForRegion(ctx, "region-c", "22222222-2222-4222-8222-222222222222")
	if err != nil {
		t.Fatal(err)
	}
	if other.ID == first.ID {
		t.Fatalf("distinct owners should not share an estate")
	}

	// Re-fetching a mapped region returns its estate without remapping.
	again, err := store.ForRegion(ctx, "region-a", "some-other-owner")
	if err != nil {
		t.Fatal(err)
	}
	if again.ID != first.ID {
		t.Fatalf("mapped region should keep its estate: %d vs %d", again.ID, first.ID)
	}
}

func TestMemoryStoreUpdateSettingsAndMembers(t *testing.T) {
	ctx := context.Background()
	store := NewMemoryStore()
	owner := "11111111-1111-4111-8111-111111111111"
	base, err := store.ForRegion(ctx, "region-a", owner)
	if err != nil {
		t.Fatal(err)
	}

	deny := true
	public := false
	name := "Private Estate"
	updated, err := store.UpdateSettings(ctx, base.ID, SettingsUpdate{
		Name: &name, PublicAccess: &public, FixedSun: &deny})
	if err != nil {
		t.Fatal(err)
	}
	if updated.Name != "Private Estate" || updated.PublicAccess || !updated.FixedSun {
		t.Fatalf("settings not applied: %#v", updated)
	}

	manager := "33333333-3333-4333-8333-333333333333"
	banned := "44444444-4444-4444-8444-444444444444"
	if _, err := store.SetMember(ctx, base.ID, manager, RoleManager, true); err != nil {
		t.Fatal(err)
	}
	withBan, err := store.SetMember(ctx, base.ID, banned, RoleBannedUser, true)
	if err != nil {
		t.Fatal(err)
	}
	if len(withBan.Managers) != 1 || withBan.Managers[0] != manager {
		t.Fatalf("manager not added: %#v", withBan.Managers)
	}
	if len(withBan.Bans) != 1 || withBan.Bans[0] != banned {
		t.Fatalf("ban not added: %#v", withBan.Bans)
	}
	// Adding the same member again is idempotent.
	dup, err := store.SetMember(ctx, base.ID, manager, RoleManager, true)
	if err != nil {
		t.Fatal(err)
	}
	if len(dup.Managers) != 1 {
		t.Fatalf("duplicate manager added: %#v", dup.Managers)
	}
	// Removing a member drops it.
	removed, err := store.SetMember(ctx, base.ID, banned, RoleBannedUser, false)
	if err != nil {
		t.Fatal(err)
	}
	if len(removed.Bans) != 0 {
		t.Fatalf("ban not removed: %#v", removed.Bans)
	}
}
