idf_build_get_property(project_dir PROJECT_DIR)

idf_component_register(
  SRCS "main.c" "wifi.c" "update.c" "mqtt.c" "certificates.c" "cjson.c" "am2320.c"
  INCLUDE_DIRS "include"
  EMBED_TXTFILES ${project_dir}/certificate-collection/ca/ca.pem
                 ${project_dir}/certificate-collection/ca/ca.der
                 ${project_dir}/certificate-collection/certificates/temperature_sensor.pem
                 ${project_dir}/certificate-collection/certificates/temperature_sensor.key
)
