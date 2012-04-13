#! /bin/sh
# Find man pages that are missing from the SEE ALSO sections of other manpages

# Run from the right path
if [ -d ../pkg ]; then
	cd ..
fi

exit_status=0
for target_path in `find pkg -type f -name '*.8' -or -name '*.5'`; do
	target=${target_path#pkg/};
	section=${target##*.};
	target=${target%.*};
	for page_path in `find pkg -type f -name '*.8' -or -name '*.5'`; do
		# A pge shouldn't reference itself
		if [ $target_path = $page_path ]; then
			continue
		fi
		grep -qm 1 "^\.Xr $target $section" $page_path
		if [ $? -ne 0 ]; then
			echo "'.Xr $target $section' missing in $page_path" >&2
			exit_status=1
		fi
	done
done
exit $exit_status
