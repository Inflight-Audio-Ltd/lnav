/**
 * Copyright (c) 2025, Timothy Stack
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * * Neither the name of Timothy Stack nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <algorithm>
#include <utility>

#include "cmd.parser.hh"

#include "data_scanner.hh"
#include "shlex.hh"

namespace lnav::command {

std::optional<std::pair<const help_text*, shlex::split_element_t>>
parsed::arg_at(int x) const
{
    for (const auto& arg : this->p_args) {
        for (const auto& se : arg.second.a_values) {
            if (se.se_origin.sf_begin <= x && x <= se.se_origin.sf_end) {
                switch (arg.second.a_help->ht_format) {
                    case help_parameter_format_t::HPF_TEXT:
                    case help_parameter_format_t::HPF_REGEX:
                    case help_parameter_format_t::HPF_TIME_FILTER_POINT: {
                        data_scanner ds(se.se_value);

                        while (true) {
                            auto tok_res = ds.tokenize2();

                            if (!tok_res) {
                                break;
                            }
                            auto tok = tok_res.value();

                            log_debug("cap b:%d  x:%d  e:%d",
                                      tok.tr_capture.c_begin,
                                      x,
                                      tok.tr_capture.c_end);
                            if (tok.tr_capture.c_begin <= x
                                && x <= tok.tr_capture.c_end)
                            {
                                return std::make_pair(
                                    arg.second.a_help,
                                    shlex::split_element_t{
                                        tok.to_string_fragment(),
                                        tok.to_string(),
                                    });
                            }
                        }
                        return std::make_pair(arg.second.a_help,
                                              shlex::split_element_t{});
                    }
                    default:
                        return std::make_pair(arg.second.a_help, se);
                }
            }
        }
    }

    for (const auto& param : this->p_help->ht_parameters) {
        const auto p_iter = this->p_args.find(param.ht_name);
        if (p_iter->second.a_values.empty()
            || param.ht_nargs == help_nargs_t::HN_ZERO_OR_MORE
            || param.ht_nargs == help_nargs_t::HN_ONE_OR_MORE)
        {
            return std::make_pair(p_iter->second.a_help,
                                  shlex::split_element_t{});
        }
    }

    return std::nullopt;
}

enum class mode_t {
    prompt,
    call,
};

static Result<parsed, lnav::console::user_message>
parse_for(mode_t mode,
          exec_context& ec,
          string_fragment args,
          const help_text& ht)
{
    parsed retval;
    shlex lexer(args);
    auto split_res = lexer.split(ec.create_resolver());

    if (split_res.isErr()) {
        auto te = split_res.unwrapErr();
        if (mode == mode_t::call) {
            return Err(
                lnav::console::user_message::error("unable to parse arguments")
                    .with_reason(te.te_msg));
        }
    }
    auto split_args = split_res.isOk() ? split_res.unwrap()
                                       : std::vector<shlex::split_element_t>{};
    auto split_index = size_t{0};

    retval.p_help = &ht;
    for (const auto& param : ht.ht_parameters) {
        auto& arg = retval.p_args[param.ht_name];
        arg.a_help = &param;
        switch (param.ht_nargs) {
            case help_nargs_t::HN_REQUIRED:
            case help_nargs_t::HN_ONE_OR_MORE: {
                if (split_index >= split_args.size()) {
                    if (mode == mode_t::call) {
                        return Err(lnav::console::user_message::error(
                            "missing required argument"));
                    }
                    continue;
                }
                break;
            }
            case help_nargs_t::HN_OPTIONAL:
            case help_nargs_t::HN_ZERO_OR_MORE: {
                if (split_index >= split_args.size()) {
                    continue;
                }
                break;
            }
        }

        do {
            const auto& se = split_args[split_index];
            switch (param.ht_format) {
                case help_parameter_format_t::HPF_TEXT:
                case help_parameter_format_t::HPF_REGEX:
                case help_parameter_format_t::HPF_TIME_FILTER_POINT: {
                    const auto& last_se = split_args.back();
                    auto sf = string_fragment{
                        se.se_origin.sf_string,
                        se.se_origin.sf_begin,
                        last_se.se_origin.sf_end,
                    };
                    arg.a_values.emplace_back(
                        shlex::split_element_t{sf, sf.to_string()});
                    split_index = split_args.size() - 1;
                    break;
                }
                case help_parameter_format_t::HPF_STRING:
                case help_parameter_format_t::HPF_FILENAME:
                case help_parameter_format_t::HPF_LOADED_FILE:
                case help_parameter_format_t::HPF_FORMAT_FIELD:
                case help_parameter_format_t::HPF_TIMEZONE: {
                    if (!param.ht_enum_values.empty()) {
                        auto enum_iter = std::find(param.ht_enum_values.begin(),
                                                   param.ht_enum_values.end(),
                                                   se.se_value);
                        if (enum_iter == param.ht_enum_values.end()) {
                            if (mode == mode_t::call) {
                                return Err(lnav::console::user_message::error(
                                    "bad enum"));
                            }
                        }
                    }

                    arg.a_values.emplace_back(se);
                    break;
                }
            }
            split_index += 1;
        } while (split_index < split_args.size()
                 && (param.ht_nargs == help_nargs_t::HN_ZERO_OR_MORE
                     || param.ht_nargs == help_nargs_t::HN_ONE_OR_MORE));
    }

    return Ok(retval);
}

parsed
parse_for_prompt(exec_context& ec, string_fragment args, const help_text& ht)
{
    return parse_for(mode_t::prompt, ec, args, ht).unwrap();
}

Result<parsed, lnav::console::user_message>
parse_for_call(exec_context& ec, string_fragment args, const help_text& ht)
{
    return parse_for(mode_t::call, ec, args, ht);
}

}  // namespace lnav::command
