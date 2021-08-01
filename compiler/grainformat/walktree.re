open Grain_parsing;

type node_t =
  | Code(Grain_parsing.Location.t)
  | Comment((Grain_parsing.Location.t, Grain_parsing.Parsetree.comment));

let allLocations: ref(list(node_t)) = ref([]);

let getNodeLoc = (node: node_t): Grain_parsing.Location.t =>
  switch (node) {
  | Code(loc) => loc
  | Comment((loc, _)) => loc
  };

let getCommentLoc = (comment: Parsetree.comment) =>
  switch (comment) {
  | Line(cmt) => cmt.cmt_loc
  | Block(cmt) => cmt.cmt_loc
  | Doc(cmt) => cmt.cmt_loc
  | Shebang(cmt) => cmt.cmt_loc
  };

// iterate the tree to make an ordered list of locations
// we can then find expressions between locations

//

let get_raw_pos_info = (pos: Lexing.position) => (
  pos.pos_fname,
  pos.pos_lnum,
  pos.pos_cnum - pos.pos_bol,
  pos.pos_bol,
);

let print_loc = (msg: string, loc: Grain_parsing.Location.t) => {
  let (file, line, startchar, _) = get_raw_pos_info(loc.loc_start);
  let (_, endline, endchar, _) = get_raw_pos_info(loc.loc_end);
  /*let endchar = loc.loc_end.pos_cnum - loc.loc_start.pos_cnum + startchar in*/

  if (startchar >= 0) {
    if (line == endline) {
      print_endline(
        msg
        ++ " "
        ++ string_of_int(line)
        ++ ":"
        ++ string_of_int(startchar)
        ++ ","
        ++ string_of_int(endchar),
      );
    } else {
      print_endline(
        msg
        ++ " "
        ++ string_of_int(line)
        ++ ":"
        ++ string_of_int(startchar)
        ++ " - "
        ++ string_of_int(endline)
        ++ ":"
        ++ string_of_int(endchar),
      );
    };
  };
};

let compare_locations =
    (loc1: Grain_parsing.Location.t, loc2: Grain_parsing.Location.t) => {
  let (_, raw1l, raw1c, _) = get_raw_pos_info(loc1.loc_start);
  let (_, raw2l, raw2c, _) = get_raw_pos_info(loc2.loc_start);

  let (_, raw1le, raw1ce, _) = get_raw_pos_info(loc1.loc_end);
  let (_, raw2le, raw2ce, _) = get_raw_pos_info(loc2.loc_end);

  let loc2startsAfter =
    if (raw2l >= raw1l) {
      if (raw2l > raw1l) {
        true;
      } else if (raw2c > raw1c) {
        true;
      } else {
        false;
      };
    } else {
      false;
    };

  let loc2finishedBefore =
    if (raw2le <= raw1le) {
      if (raw2le < raw1le) {
        true;
      } else if (raw2ce < raw1ce) {
        true;
      } else {
        false;
      };
    } else {
      false;
    };

  let res =
    if (loc2startsAfter && loc2finishedBefore) {
      0;
    } else if (loc2startsAfter) {
      (-1);
    } else {
      1;
    };

  res;
};

let walktree =
    (
      statements: list(Grain_parsing.Parsetree.toplevel_stmt),
      comments: list(Grain_parsing.Parsetree.comment),
    ) => {
  //let iterator = {...Ast_iterator.default_iterator,};

  let comment_locations =
    List.map(c => Comment((getCommentLoc(c), c)), comments);

  allLocations := comment_locations;

  let iter_location = (self, location) => {
    // print_loc("walked location:", location);
    allLocations := List.append(allLocations^, [Code(location)]);
  };

  let iterator = {...Ast_iterator.default_iterator, location: iter_location};

  List.iter(iterator.toplevel(iterator), statements);

  allLocations :=
    List.sort(
      (node1: node_t, node2: node_t) => {
        let loc1 = getNodeLoc(node1);

        let loc2 = getNodeLoc(node2);

        compare_locations(loc1, loc2);
      },
      allLocations^,
    );
  // List.iter(l => print_loc("Sorted loc:", l), allLocations^);
  // List.iter(
  //   n =>
  //     switch (n) {
  //     | Comment((l, c)) => print_loc("comment", l)
  //     | Code(l) => print_loc("code", l)
  //     },
  //   allLocations^,
  // );
};

// let get_loc_node_after =
//     (loc: Grain_parsing.Location.t, allLocations: list(node_t))
//     : option(node_t) => {
//   // print_loc("looking for a node afte", loc);
//   let (_, raw1l, raw1c, _) = get_raw_pos_info(loc.loc_start);
//   let (_, raw1le, raw1ce, _) = get_raw_pos_info(loc.loc_end);

//   let afterloc: option(node_t) =
//     List.fold_left(
//       (acc, node: node_t) => {
//         switch (acc) {
//         | Some(_) => acc // nothing more to do
//         | None =>
//           let l = getNodeLoc(node);
//           let (_, raw2l, raw2c, _) = get_raw_pos_info(l.loc_start);
//           // let (_, raw2le, raw2ce, _) = get_raw_pos_info(l.loc_end);

//           if (raw2l > raw1l) {
//             //  print_loc("A match", l);
//             Some(node);
//           } else if (raw2l == raw1l) {
//             if (raw2c > raw1ce) {
//               //  print_loc("B match", l);
//               Some(node);
//             } else {
//               None;
//             };
//           } else {
//             None;
//           };
//         }
//       },
//       None,
//       allLocations,
//     );

//   afterloc;
// };

// let get_node_after = (loc: Grain_parsing.Location.t): option(node_t) => {
//   let nodes =
//     List.filter(
//       n =>
//         switch (n) {
//         | Code(_) => true
//         | Comment(_) => false
//         },
//       allLocations^,
//     );
//   get_loc_node_after(loc, nodes);
// };

// let get_comment_after = (loc: Grain_parsing.Location.t): option(node_t) => {
//   let nodes =
//     List.filter(
//       n =>
//         switch (n) {
//         | Code(_) => false
//         | Comment(_) => true
//         },
//       allLocations^,
//     );
//   get_loc_node_after(loc, nodes);
// };

// let get_next_after = (loc: Grain_parsing.Location.t): option(node_t) => {
//   get_loc_node_after(loc, allLocations^);
// };

let partitionNodeComments =
    (loc: Grain_parsing.Location.t)
    : (
        list(Grain_parsing.Parsetree.comment),
        list(Grain_parsing.Parsetree.comment),
      ) => {
  print_loc("****** partitionNodeComments", loc);

  let (preceeding, following) =
    List.fold_left(
      (acc, n) => {
        let (preceeding, following) = acc;
        let nodeLoc = getNodeLoc(n);

        if (compare_locations(loc, nodeLoc) > 0) {
          (preceeding @ [n], following);
        } else if (compare_locations(loc, nodeLoc) < 0) {
          (preceeding, following @ [n]);
        } else {
          acc;
        };
      },
      ([], []),
      allLocations^,
    );

  // print_endline(
  //   "We have a preceeding count of "
  //   ++ string_of_int(List.length(preceeding)),
  // );

  // print_endline(
  //   "We have a following count of " ++ string_of_int(List.length(following)),
  // );
  // List.iter(n => print_loc("post", getNodeLoc(n)), following);

  let skip = ref(false);
  let preComments =
    List.fold_left(
      (acc, n) =>
        if (skip^) {
          acc;
        } else {
          switch (n) {
          | Comment((l, c)) => [c] @ acc
          | Code(_) =>
            skip := true;
            acc;
          };
        },
      [],
      List.rev(preceeding),
    );

  skip := false;
  let postComments =
    List.fold_left(
      (acc, n) =>
        if (skip^) {
          acc;
        } else {
          switch (n) {
          | Comment((l, c)) => acc @ [c]
          | Code(_) =>
            skip := true;
            acc;
          };
        },
      [],
      following,
    );

  // look for all comments after this node untl we hit a node not a comment

  // assume we use them so remove them

  let cleanedList =
    List.filter(
      n => {
        switch (n) {
        | Code(_) => true
        | Comment((loc, comment)) =>
          if (List.mem(comment, preComments)
              || List.mem(comment, postComments)) {
            false;
          } else {
            true;
          }
        }
      },
      allLocations^,
    );

  allLocations := cleanedList;

  // print_endline(
  //   "We  finish with preceeding comments of  "
  //   ++ string_of_int(List.length(preComments)),
  // );

  // List.iter(n => print_loc("pre", getNodeLoc(n)), preceeding);
  // print_endline(
  //   "We finsh with a following count of "
  //   ++ string_of_int(List.length(postComments)),
  // );

  (preComments, postComments);
};
