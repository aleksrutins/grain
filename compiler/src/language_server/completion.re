open Grain_utils;
open Grain_typed;
open Grain_diagnostics;

// This is the full enumeration of all CompletionItemKind as declared by the language server
// protocol (https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#completionItemKind),
// but not all will be used by Grain LSP
[@deriving (enum, yojson)]
type completion_item_kind =
  // Since these are using ppx_deriving enum, order matters
  | [@value 1] CompletionItemKindText
  | CompletionItemKindMethod
  | CompletionItemKindFunction
  | CompletionItemKindConstructor
  | CompletionItemKindField
  | CompletionItemKindVariable
  | CompletionItemKindClass
  | CompletionItemKindInterface
  | CompletionItemKindModule
  | CompletionItemKindProperty
  | CompletionItemKindUnit
  | CompletionItemKindValue
  | CompletionItemKindEnum
  | CompletionItemKindKeyword
  | CompletionItemKindSnippet
  | CompletionItemKindColor
  | CompletionItemKindFile
  | CompletionItemKindReference
  | CompletionItemKindFolder
  | CompletionItemKindEnumMember
  | CompletionItemKindConstant
  | CompletionItemKindStruct
  | CompletionItemKindEvent
  | CompletionItemKindOperator
  | CompletionItemKindTypeParameter;

[@deriving (enum, yojson)]
type completion_trigger_kind =
  // Since these are using ppx_deriving enum, order matters
  | [@value 1] CompletionTriggerInvoke
  | CompletionTriggerCharacter
  | CompletionTriggerForIncompleteCompletions;

let completion_item_kind_to_yojson = severity =>
  completion_item_kind_to_enum(severity) |> [%to_yojson: int];
let completion_item_kind_of_yojson = json =>
  Result.bind(json |> [%of_yojson: int], value => {
    switch (completion_item_kind_of_enum(value)) {
    | Some(severity) => Ok(severity)
    | None => Result.Error("Invalid enum value")
    }
  });

let completion_trigger_kind_to_yojson = kind =>
  completion_trigger_kind_to_enum(kind) |> [%to_yojson: int];
let completion_trigger_kind_of_yojson = json =>
  Result.bind(json |> [%of_yojson: int], value => {
    switch (completion_trigger_kind_of_enum(value)) {
    | Some(kind) => Ok(kind)
    | None => Result.Error("Invalid enum value")
    }
  });

[@deriving yojson]
type completion_item = {
  label: string,
  kind: completion_item_kind,
  detail: string,
  documentation: string,
};

[@deriving yojson({strict: false})]
type completion_context = {
  [@key "triggerKind"]
  trigger_kind: completion_trigger_kind,
  [@key "triggerCharacter"] [@default None]
  trigger_character: option(string),
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#completionParams
module RequestParams = {
  [@deriving yojson({strict: false})]
  type t = {
    [@key "textDocument"]
    text_document: Protocol.text_document_identifier,
    position: Protocol.position,
    [@default None]
    context: option(completion_context),
  };
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#completionList
module ResponseResult = {
  [@deriving yojson]
  type t = {
    isIncomplete: bool,
    items: list(completion_item),
  };
};

// Original implementation https://github.com/jaredly/reason-language-server/blob/ce1b3f8ddb554b6498c2a83ea9c53a6bdf0b6081/src/analyze/PartialParser.re#L178-L198
let find_completable = (text, offset) => {
  let rec loop = i => {
    Trace.log(text ++ " " ++ string_of_int(i) ++ " " ++ string_of_int(offset))
    i < 0
      ? Some(String_utils.slice(~first=0, ~last=offset + 1, text))
      : (
        switch (String_utils.char_at(text, i - 1)) {
        | Some('a' .. 'z' | 'A' .. 'Z' | '0' .. '9' | '.' | '_') =>
          Trace.log_opt(String_utils.char_at(text, i) |> Option.bind(_, c => Some(String.make(1, c))));
          loop(i - 1)
        | _ =>
          let res = Some(
            String_utils.slice(
              ~first=i,
              ~last=offset,
              text,
            ),
          );
          Trace.log_opt(res);
          res
        }
      );
  };
  loop(offset - 1);
};

let get_original_text = (documents, uri, line, char) =>
  // try and find the code we are completing in the original source
  switch (Hashtbl.find_opt(documents, uri)) {
  | None => None
  | Some(source_code) =>
    let lines = String.split_on_char('\n', source_code);
    Trace.log(source_code ++ "\n" ++ string_of_int(line));
    let line = List.nth_opt(lines, line);
    Trace.log_opt(line);
    Option.bind(line, line => find_completable(line, char));
  };

// maps Grain types to LSP CompletionItemKind
let rec get_kind = (desc: Types.type_desc) =>
  switch (desc) {
  | TTyVar(_) => CompletionItemKindVariable
  | TTyArrow(_) => CompletionItemKindFunction
  | TTyTuple(_) => CompletionItemKindStruct
  | TTyRecord(_) => CompletionItemKindStruct
  | TTyConstr(_) => CompletionItemKindConstructor
  | TTySubst(s) => get_kind(s.desc)
  | TTyLink(t) => get_kind(t.desc)
  | _ => CompletionItemKindText
  };

let send_completion =
    (~id: Protocol.message_id, completions: list(completion_item)) => {
  Protocol.response(
    ~id,
    ResponseResult.to_yojson({isIncomplete: false, items: completions}),
  );
};

module Resolution = {
  // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#completionItem
  module RequestParams = {
    // TODO: implement the rest of the fields
    [@deriving yojson({strict: false})]
    type t = {label: string};
  };

  // As per https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_completion
  // If computing full completion items is expensive, servers can additionally provide a handler for
  // the completion item resolve request (‘completionItem/resolve’). This request is sent when a
  // completion item is selected in the user interface.
  let process =
      (
        ~id: Protocol.message_id,
        ~compiled_code: Hashtbl.t(Protocol.uri, Lsp_types.code),
        ~cached_code: Hashtbl.t(Protocol.uri, Lsp_types.code),
        ~documents: Hashtbl.t(Protocol.uri, string),
        params: RequestParams.t,
      ) => {
    // Right now we just resolve nothing to clear the client's request
    // In future we may want to send more details back with Graindoc details for example
    send_completion(
      ~id,
      [],
    );
  };
};

let complete_all = (compiled_code: Lsp_types.code) => {
  // Autocompete anything in scope
  let values: list((string, Types.value_description)) =
    Env.fold_values(
      (tag, path, vd, acc) => {
        List.append(acc, [(Printtyp.string_of_path(path), vd)])
      },
      None,
      compiled_code.program.env,
      [],
    );

  List.map(
    ((i: string, l: Types.value_description)) => {
      let item: completion_item = {
        label: i,
        kind: get_kind(l.val_type.desc),
        detail: Printtyp.string_of_type_scheme(l.val_type),
        documentation: "",
      };
      item;
    },
    values,
  );
}


let process =
    (
      ~id: Protocol.message_id,
      ~compiled_code: Hashtbl.t(Protocol.uri, Lsp_types.code),
      ~cached_code: Hashtbl.t(Protocol.uri, Lsp_types.code),
      ~documents: Hashtbl.t(Protocol.uri, string),
      params: RequestParams.t,
    ) => {
  let completable =
    get_original_text(
      documents,
      params.text_document.uri,
      params.position.line,
      params.position.character,
    );
    switch (Hashtbl.find_opt(cached_code, params.text_document.uri)) {
    | None => send_completion(~id, [])
    | Some(compiled_code) =>
      switch (completable) {
      | None => send_completion(~id, complete_all(compiled_code))
      | Some(prior_version_text) =>
        let text = prior_version_text;
        Trace.log(text);
          // switch (params.context) {
          // | None => prior_version_text
          // | Some(ctx) =>
          //   switch (ctx.trigger_kind) {
          //   | CompletionTriggerCharacter =>
          //     prior_version_text
          //     ++ Option.value(~default="", ctx.trigger_character)
          //   | _ => prior_version_text
          //   }
          // };
        let modules =
          Env.fold_modules(
            (tag, path, decl, acc) => {
              List.append(acc, [Printtyp.string_of_path(path)])
            },
            None,
            compiled_code.program.env,
            [],
          );

        let completions =
          switch (String_utils.char_at(text, 0)) {
          | Some('A' .. 'Z') =>
            // autocomplete modules
            switch (String.rindex(text, '.')) {
            | exception exn =>
              let types =
                Env.fold_types(
                  (tag, path, (type_decl, type_descs), acc) => {
                    List.append(acc, [Printtyp.string_of_path(path)])
                  },
                  None,
                  compiled_code.program.env,
                  [],
                );

              let converted_modules =
                List.map(
                  (m: string) => {
                    let item: completion_item = {
                      label: m,
                      kind: CompletionItemKindModule,
                      detail: "",
                      documentation: "",
                    };
                    item;
                  },
                  modules,
                );

              let converted_types =
                List.map(
                  (t: string) => {
                    let item: completion_item = {
                      label: t,
                      kind: CompletionItemKindStruct,
                      detail: "",
                      documentation: "",
                    };
                    item;
                  },
                  types,
                );

              converted_modules @ converted_types;
            | pos =>
              // find module name

              let mod_name = String_utils.slice(~first=0, ~last=pos, text);
              let ident: Ident.t = {name: mod_name, stamp: 0, flags: 0};

              // only look up completions for imported modules
              if (!List.exists((m: string) => m == mod_name, modules)) {
                [];
              } else {
                List.map(
                  (m: Modules.export) => {
                    let kind =
                      switch (m.kind) {
                      | Function => CompletionItemKindFunction
                      | Value => CompletionItemKindValue
                      | Record => CompletionItemKindStruct
                      | Enum => CompletionItemKindEnum
                      | Abstract => CompletionItemKindTypeParameter
                      | Exception => CompletionItemKindTypeParameter
                      };

                    {
                      label: m.name,
                      kind,
                      detail: m.signature,
                      documentation: "",
                    };
                  },
                  Modules.get_exports(~path=PIdent(ident), compiled_code.program),
                );
              };
            }

          | _ =>
            complete_all(compiled_code);
          };

        send_completion(~id, completions);
    };
  };
};
