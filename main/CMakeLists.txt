idf_build_get_property(project_dir PROJECT_DIR)

idf_component_register(
  SRCS "main.c" "wifi.c" "update.c"
  INCLUDE_DIRS "include"
  EMBED_TXTFILES ${project_dir}/server_certs/wifiupdate.pem
)
