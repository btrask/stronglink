ifndef ROOT_DIR
	ROOT_DIR := ..
endif
ifndef BUILD_DIR
	BUILD_DIR := $(ROOT_DIR)/build/www
endif
ifndef CLIENT_DIR
	CLIENT_DIR := $(ROOT_DIR)/client
endif

CLOSURE_FLAGS := --compilation_level WHITESPACE_ONLY --formatting PRETTY_PRINT

CLIENT_SCRIPTS := \
	$(CLIENT_DIR)/external/marked.js \
	$(CLIENT_DIR)/external/json2.js \
	$(CLIENT_DIR)/utilities/DOM.js \
	$(CLIENT_DIR)/utilities/url.js \
	$(CLIENT_DIR)/index.js

CLIENT_FILES := \
	robots.txt \
	favicon.ico \
	index.html \
	index.css \
	index.js
CLIENT_FILES := $(addprefix $(BUILD_DIR)/,$(CLIENT_FILES))

CLIENT_GZIPS := $(addsuffix .gz,$(CLIENT_FILES))

client: $(CLIENT_FILES) $(CLIENT_GZIPS)

clean-client:
	-rm -rf $(BUILD_DIR)

$(BUILD_DIR)/index.js: $(CLIENT_DIR)/utilities/externs.js $(CLIENT_SCRIPTS)
	-mkdir -p $(dir $@)
	java -jar $(ROOT_DIR)/deps/compiler-latest/compiler.jar $(addprefix --js=,$(filter-out $<,$^)) --js_output_file=$@ --language_in=ECMASCRIPT5 --externs $< $(CLOSURE_FLAGS) #--compilation_level ADVANCED_OPTIMIZATIONS

$(BUILD_DIR)/%.css: $(CLIENT_DIR)/%.css
	-mkdir -p $(dir $@)
	cat $+ | java -jar $(ROOT_DIR)/deps/yuicompressor-2.4.2/build/yuicompressor-2.4.2.jar --type css --charset utf-8 -o $@

$(BUILD_DIR)/%: $(CLIENT_DIR)/%
	-mkdir -p $(dir $@)
	cp -R $< $@

$(BUILD_DIR)/%.gz: $(BUILD_DIR)/%
	-mkdir -p $(dir $@)
	gzip -nc9 $< > $@

.PHONY: client clean

