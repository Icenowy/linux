# Locate the userspace headers of dmabuf_exporter
#

get_filename_component(DMABUF_EXPORTER_PREFIX "${CMAKE_CURRENT_LIST_FILE}" PATH)

set (DMABUF_EXPORTER_FOUND TRUE)
set (DMABUF_EXPORTER_INCLUDE_DIR ${DMABUF_EXPORTER_PREFIX}/uapi)

