#!/bin/bash
# Remove trailing whitespace only from newly added lines in staged files

# Get staged files (Added, Copied, Modified)
files=$(git diff --cached --name-only --diff-filter=ACM)

for file in $files; do
	echo "working on $file"
  # Only process text files
  if file "$file" | grep -q text; then
    # Extract added lines with line numbers
    added_lines=$(git diff --cached -U0 "$file" | grep '^+' | grep -v '^+++' | cut -c2-)
		echo "working on $file - inside"

    if [ -n "$added_lines" ]; then
      # Clean trailing whitespace in working copy
			echo "FIXING FILE $file"
      sed -i 's/[[:space:]]\+$//' "$file"
      # Re‑add to staging
      git add "$file"
    fi
  fi
done

