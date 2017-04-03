@Remove_unnecessary_pointer_checks1@
expression x;
identifier release =~ "^(?x)
(?:(?:(?:pkg(?:db_(?:sqlite_)?it
            |  _(?:audit
                |  conflict
                |  deb
                |  file
                |  manifest_keys
                |  option
                |  provide
                |  repo_binary_update_item
                |  shlib
                )
            )?
      | rsa
      | sbuf
      )_)?free
| free_(?:file_attr|percent_esc)
| load_repositories
| pkg(?:_reset
     |  db_(?:close|sqlite_it_reset)
     )
| sbuf_reset
)$";
@@
-if (\(x != 0 \| x != NULL\))
    release(x);

@Remove_unnecessary_pointer_checks2@
expression x;
identifier release =~ "^(?x)
(?:(?:(?:pkg(?:db_(?:sqlite_)?it
            |  _(?:audit
                |  conflict
                |  deb
                |  file
                |  manifest_keys
                |  option
                |  provide
                |  repo_binary_update_item
                |  shlib
                )
            )?
      | rsa
      | sbuf
      )_)?free
| free_(?:file_attr|percent_esc)
| load_repositories
| pkg(?:_reset
     |  db_(?:close|sqlite_it_reset)
     )
| sbuf_reset
)$";
@@
-if (\(x != 0 \| x != NULL\)) {
    release(x);
    x = \(0 \| NULL\);
-}

@Remove_unnecessary_pointer_checks3@
expression a, b;
identifier release =~ "^(?x)
(?:(?:(?:pkg(?:db_(?:sqlite_)?it
            |  _(?:audit
                |  conflict
                |  deb
                |  file
                |  manifest_keys
                |  option
                |  provide
                |  repo_binary_update_item
                |  shlib
                )
            )?
      | rsa
      | sbuf
      )_)?free
| free_(?:file_attr|percent_esc)
| load_repositories
| pkg(?:_reset
     |  db_(?:close|sqlite_it_reset)
     )
| sbuf_reset
)$";
@@
-if (\(a != 0 \| a != NULL\) && \(b != 0 \| b != NULL\))
+if (a)
    release(b);

@Remove_unnecessary_pointer_checks4@
expression a, b;
identifier release =~ "^(?x)
(?:(?:(?:pkg(?:db_(?:sqlite_)?it
            |  _(?:audit
                |  conflict
                |  deb
                |  file
                |  manifest_keys
                |  option
                |  provide
                |  repo_binary_update_item
                |  shlib
                )
            )?
      | rsa
      | sbuf
      )_)?free
| free_(?:file_attr|percent_esc)
| load_repositories
| pkg(?:_reset
     |  db_(?:close|sqlite_it_reset)
     )
| sbuf_reset
)$";
@@
-if (\(a != 0 \| a != NULL\) && \(b != 0 \| b != NULL\)) {
+if (a) {
    release(b);
    b = \(0 \| NULL\);
 }
