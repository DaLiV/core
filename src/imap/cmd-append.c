/* Copyright (C) 2002 Timo Sirainen */

#include "common.h"
#include "ioloop.h"
#include "ostream.h"
#include "commands.h"
#include "imap-parser.h"
#include "imap-date.h"

#include <sys/time.h>

/* Returns -1 = error, 0 = need more data, 1 = successful. flags and
   internal_date may be NULL as a result, but mailbox and msg_size are always
   set when successful. */
static int validate_args(struct client *client, const char **mailbox,
			 struct imap_arg_list **flags,
			 const char **internal_date,
			 uoff_t *msg_size, unsigned int count)
{
	struct imap_arg *args;

	i_assert(count >= 2 && count <= 4);

	*flags = NULL;
	*internal_date = NULL;

	if (!client_read_args(client, count, IMAP_PARSE_FLAG_LITERAL_SIZE,
			      &args))
		return 0;

	switch (count) {
	case 2:
		/* do we have flags or internal date parameter? */
		if (args[1].type == IMAP_ARG_LIST ||
		    args[1].type == IMAP_ARG_STRING)
			return validate_args(client, mailbox, flags,
					     internal_date, msg_size, 3);

		break;
	case 3:
		/* do we have both flags and internal date? */
		if (args[1].type == IMAP_ARG_LIST &&
		    args[2].type == IMAP_ARG_STRING)
			return validate_args(client, mailbox, flags,
					     internal_date, msg_size, 4);

		if (args[1].type == IMAP_ARG_LIST)
			*flags = IMAP_ARG_LIST(&args[1]);
		else if (args[1].type == IMAP_ARG_STRING)
			*internal_date = IMAP_ARG_STR(&args[1]);
		else
			return -1;
		break;
	case 4:
		/* we have all parameters */
		*flags = IMAP_ARG_LIST(&args[1]);
		*internal_date = IMAP_ARG_STR(&args[2]);
		break;
	default:
                i_unreached();
	}

	/* check that mailbox and message arguments are ok */
	*mailbox = imap_arg_string(&args[0]);
	if (*mailbox == NULL)
		return -1;

	if (args[count-1].type != IMAP_ARG_LITERAL_SIZE)
		return -1;

	*msg_size = IMAP_ARG_LITERAL_SIZE(&args[count-1]);
	return 1;
}

int cmd_append(struct client *client)
{
	struct imap_arg_list *flags_list;
	struct mailbox *box;
	struct mail_full_flags flags;
	time_t internal_date;
	const char *mailbox, *internal_date_str;
	uoff_t msg_size;
	int failed, timezone_offset;

	/* <mailbox> [<flags>] [<internal date>] <message literal> */
	switch (validate_args(client, &mailbox, &flags_list,
			      &internal_date_str, &msg_size, 2)) {
	case -1:
		/* error */
		client_send_command_error(client, NULL);
		return TRUE;
	case 0:
		/* need more data */
		return FALSE;
	}

	if (flags_list != NULL) {
		if (!client_parse_mail_flags(client, flags_list->args,
					     &flags))
			return TRUE;
	} else {
		memset(&flags, 0, sizeof(flags));
	}

	if (internal_date_str == NULL) {
		/* no time given, default to now. */
		internal_date = ioloop_time;
                timezone_offset = ioloop_timezone.tz_minuteswest;
	} else if (!imap_parse_datetime(internal_date_str, &internal_date,
					&timezone_offset)) {
		client_send_tagline(client, "BAD Invalid internal date.");
		return TRUE;
	}

	/* open the mailbox */
	if (!client_verify_mailbox_name(client, mailbox, TRUE, FALSE))
		return TRUE;

	box = client->storage->open_mailbox(client->storage,
					    mailbox, FALSE, TRUE);
	if (box == NULL) {
		client_send_storage_error(client);
		return TRUE;
	}

	o_stream_send(client->output, "+ OK\r\n", 6);
	o_stream_flush(client->output);

	/* save the mail */
	failed = !box->save(box, &flags, internal_date, timezone_offset,
			    client->input, msg_size);
	box->close(box);

	if (failed) {
		client_send_storage_error(client);
	} else {
		client_sync_full(client);
		client_send_tagline(client, "OK Append completed.");
	}
	return TRUE;
}
