#!/usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	list_repos \
	list_enabled \
	list_disabled \
	override_disable \
	override_enable \
	override_inherit_enable \
	override_reset \
	override_shown_in_vv \
	override_unknown_repo \
	override_unknown_action

test_setup()
{
	mkdir -p reposconf
	cat > reposconf/test.conf << EOF
repo_a: {
    url: "file:///tmp/repo_a",
    enabled: true
}
repo_b: {
    url: "file:///tmp/repo_b",
    enabled: false
}
EOF
}

list_repos_body()
{
	test_setup

	atf_check \
		-o match:'repo_a' \
		-o match:'repo_b' \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" repositories -l
}

list_enabled_body()
{
	test_setup

	atf_check \
		-o match:'repo_a' \
		-o not-match:'repo_b' \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" repositories -le
}

list_disabled_body()
{
	test_setup

	atf_check \
		-o not-match:'repo_a' \
		-o match:'repo_b' \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" repositories -ld
}

override_disable_body()
{
	test_setup

	# repo_a is enabled in config; disable it via override
	atf_check \
		-o match:'has been disabled' \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" repositories repo_a disable

	# The override file should exist
	atf_check -s exit:0 test -f repos_state/disable/repo_a

	# repo_a should now show as disabled with override
	atf_check \
		-o match:'enabled.*no.*overridden by pkg' \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" repositories repo_a

	# It should appear in disabled list now
	atf_check \
		-o match:'repo_a' \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" repositories -ld
}

override_enable_body()
{
	test_setup

	# repo_b is disabled in config; enable it via override
	atf_check \
		-o match:'has been enabled' \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" repositories repo_b enable

	# The override file should exist
	atf_check -s exit:0 test -f repos_state/enable/repo_b

	# repo_b should now show as enabled with override
	atf_check \
		-o match:'enabled.*yes.*overridden by pkg' \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" repositories repo_b

	# It should appear in enabled list now
	atf_check \
		-o match:'repo_b' \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" repositories -le
}

override_inherit_enable_body()
{

	# This test isn't very well placed, but we don't currently have an
	# obvious place for explicit tests for pkg.conf.  We default
	# repositories to enabled, but that default only really be applied the
	# first time we encounter a definition.  Later overrides should be
	# doable without having to re-specify whether the repository should
	# remain enabled or disabled.
	mkdir -p reposconf
	cat > reposconf/base.conf << EOF
repo_a: {
    url: "file:///tmp/repo_a",
    enabled: false
}
EOF
	cat > reposconf/override.conf << EOF
repo_a: {
    priority: 5
}
EOF

	atf_check \
		-o match:'repo_a' \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" repositories -ld
}

override_reset_body()
{
	test_setup

	# Disable repo_a, then reset
	atf_check -o ignore -s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" repositories repo_a disable

	atf_check \
		-o match:'override removed' \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" repositories repo_a reset

	# Override file should be gone
	atf_check -s exit:1 test -f repos_state/disable/repo_a

	# Back to config value (enabled, no override)
	atf_check \
		-o match:'enabled.*yes' \
		-o not-match:'overridden' \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" repositories repo_a
}

override_shown_in_vv_body()
{
	test_setup

	atf_check -o ignore -s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" repositories repo_a disable

	atf_check \
		-o match:'repo_a' \
		-o match:'overridden by pkg' \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" -vv

	atf_check -o ignore -s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" repositories repo_a reset
}

override_unknown_repo_body()
{
	test_setup

	atf_check \
		-e match:'Unknown repository' \
		-s exit:1 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" repositories nonexistent disable
}

override_unknown_action_body()
{
	test_setup

	atf_check \
		-e match:'Unknown action' \
		-s exit:1 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" repositories repo_a bogus
}
