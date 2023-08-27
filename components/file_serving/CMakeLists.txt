idf_component_register(SRCS "connect.c" "file_server.c"
                    INCLUDE_DIRS "."
                    EMBED_FILES "favicon.ico" "upload_script.html"
                    REQUIRES "esp_http_server"
                    
                    )
