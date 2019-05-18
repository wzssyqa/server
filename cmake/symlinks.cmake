# MariaDB names for executables
list(APPEND MARIADB_SYMLINK_NAMES "mysql" "mariadb")
list(APPEND MARIADB_SYMLINK_NAMES "mysqlaccess" "mariadb-access")
list(APPEND MARIADB_SYMLINK_NAMES "mysqladmin" "mariadb-admin")
list(APPEND MARIADB_SYMLINK_NAMES "mariabackup" "mariadb-backup")
list(APPEND MARIADB_SYMLINK_NAMES "mysqlbinlog" "mariadb-binlog")
list(APPEND MARIADB_SYMLINK_NAMES "mysqlcheck" "mariadb-check")
list(APPEND MARIADB_SYMLINK_NAMES "mysql_client_test_embedded" "mariadb-client-test-embedded")
list(APPEND MARIADB_SYMLINK_NAMES "mariadb_config" "mariadb-config")
list(APPEND MARIADB_SYMLINK_NAMES "mysql_convert_table_format" "mariadb-convert-table-format")
list(APPEND MARIADB_SYMLINK_NAMES "mysqldump" "mariadb-dump")
list(APPEND MARIADB_SYMLINK_NAMES "mysqldumpslow" "mariadb-dumpslow")
list(APPEND MARIADB_SYMLINK_NAMES "mysql_embedded" "mariadb-embedded")
list(APPEND MARIADB_SYMLINK_NAMES "mysql_find_rows" "mariadb-find-rows")
list(APPEND MARIADB_SYMLINK_NAMES "mysql_fix_extensions" "mariadb-fix-extensions")
list(APPEND MARIADB_SYMLINK_NAMES "mysqlhotcopy" "mariadb-hotcopy")
list(APPEND MARIADB_SYMLINK_NAMES "mysqlimport" "mariadb-import")
list(APPEND MARIADB_SYMLINK_NAMES "mysql_install_db" "mariadb-install-db")
list(APPEND MARIADB_SYMLINK_NAMES "mysql_ldb" "mariadb-ldb")
list(APPEND MARIADB_SYMLINK_NAMES "mysql_plugin" "mariadb-plugin")
list(APPEND MARIADB_SYMLINK_NAMES "mysql_secure_installation" "mariadb-secure-installation")
list(APPEND MARIADB_SYMLINK_NAMES "mysql_setpermission" "mariadb-setpermission")
list(APPEND MARIADB_SYMLINK_NAMES "mysqlshow" "mariadb-show")
list(APPEND MARIADB_SYMLINK_NAMES "mysqlslap" "mariadb-slap")
list(APPEND MARIADB_SYMLINK_NAMES "mysqltest" "mariadb-test")
list(APPEND MARIADB_SYMLINK_NAMES "mysqltest_embedded" "mariadb-test-embedded")
list(APPEND MARIADB_SYMLINK_NAMES "mytop" "mariadb-top")
list(APPEND MARIADB_SYMLINK_NAMES "mysql_tzinfo_to_sql" "mariadb-tzinfo-to-sql")
list(APPEND MARIADB_SYMLINK_NAMES "mysql_upgrade" "mariadb-upgrade")
list(APPEND MARIADB_SYMLINK_NAMES "mysql_upgrade_service" "mariadb-upgrade-service")
list(APPEND MARIADB_SYMLINK_NAMES "mysql_upgrade_wizard" "mariadb-upgrade-wizard")
list(APPEND MARIADB_SYMLINK_NAMES "mysql_waitpid" "mariadb-waitpid")
list(APPEND MARIADB_SYMLINK_NAMES "mysqld" "mariadbd")
list(APPEND MARIADB_SYMLINK_NAMES "mysqld_multi" "mariadbd-multi")
list(APPEND MARIADB_SYMLINK_NAMES "mysqld_safe" "mariadbd-safe")
list(APPEND MARIADB_SYMLINK_NAMES "mysqld_safe_helper" "mariadbd-safe-helper")

# Add MariaDB symlinks
macro(CREATE_MARIADB_SYMLINK src comp)
  # Find the MariaDB name for executable
  list(FIND MARIADB_SYMLINK_NAMES ${src} _index)

  if (${_index} GREATER -1)
    MATH(EXPR _index "${_index}+1")
    list(GET MARIADB_SYMLINK_NAMES ${_index} _name)
    set(mariadbname ${_name})
  endif()

  if (mariadbname)
    set(dest ${mariadbname})
    set(symlink_install_dir ${INSTALL_BINDIR})

    # adjust install location if needed
    if(${dest} MATCHES "mariadb-install-db")
      set(symlink_install_dir ${INSTALL_SCRIPTDIR})
    endif()

    CREATE_MARIADB_SYMLINK_IN_DIR(${src} ${dest} ${symlink_install_dir} ${comp})
  endif()
endmacro(CREATE_MARIADB_SYMLINK)

# Add MariaDB symlinks in directory
macro(CREATE_MARIADB_SYMLINK_IN_DIR src dest dir comp)
  if(NOT WIN32)
    add_custom_target(
      symlink_${dest}_${comp} ALL
      DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${dest}
    )

    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${dest} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${src} ${dest}
      COMMENT "mklink ${src} -> ${dest}")

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${dest} DESTINATION ${dir} COMPONENT ${comp})
  endif()
endmacro(CREATE_MARIADB_SYMLINK_IN_DIR)
